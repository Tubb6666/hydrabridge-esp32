#include "event_log.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
static const char *TAG = "event_log";
#endif

/* ---------------- internal state ---------------- */

static event_log_entry_t g_ring[EVENT_LOG_CAPACITY];
static size_t            g_head;     /* index of next write slot */
static size_t            g_count;    /* current number of valid entries */
static uint64_t (*g_clock_fn)(void) = NULL;

/* ---------------- helpers ---------------- */

static uint64_t default_clock(void)
{
#ifdef ESP_PLATFORM
    return (uint64_t)(esp_timer_get_time() / 1000);
#else
    return 0;
#endif
}

static uint64_t now_ms(void)
{
    return g_clock_fn ? g_clock_fn() : default_clock();
}

static void safe_copy(char *dst, size_t cap, const char *src)
{
    if (cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Find the next case-insensitive occurrence of `needle` in `haystack`
 * starting at `from`, or return SIZE_MAX if not found. */
static size_t find_ci(const char *haystack, size_t hay_len, size_t from, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0 || nlen > hay_len) return SIZE_MAX;
    for (size_t i = from; i + nlen <= hay_len; ++i) {
        size_t k = 0;
        while (k < nlen) {
            unsigned char a = (unsigned char)haystack[i + k];
            unsigned char b = (unsigned char)needle[k];
            if (tolower(a) != tolower(b)) break;
            ++k;
        }
        if (k == nlen) return i;
    }
    return SIZE_MAX;
}

static bool is_value_terminator(char c)
{
    return c == '\0' || c == ' ' || c == '\t' || c == '\n' || c == '\r'
        || c == ',' || c == ';' || c == '"' || c == '\'' || c == '}' || c == ')';
}

/* ---------------- public API ---------------- */

void event_log_init(void)
{
    g_head = 0;
    g_count = 0;
    g_clock_fn = NULL;
    memset(g_ring, 0, sizeof g_ring);
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "init capacity=%d", EVENT_LOG_CAPACITY);
#endif
}

void event_log_reset(void)
{
    g_head = 0;
    g_count = 0;
    memset(g_ring, 0, sizeof g_ring);
}

void event_log_set_clock_fn(uint64_t (*fn)(void))
{
    g_clock_fn = fn;
}

void event_log_redact(char *buf, size_t buflen)
{
    if (!buf || buflen == 0) return;
    size_t len = strnlen(buf, buflen);
    static const char *keys[] = {
        "password", "passwd", "pwd", "secret"
    };
    size_t cursor = 0;
    while (cursor < len) {
        size_t best = SIZE_MAX;
        size_t best_key_len = 0;
        for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); ++k) {
            size_t at = find_ci(buf, len, cursor, keys[k]);
            if (at != SIZE_MAX && at < best) {
                best = at;
                best_key_len = strlen(keys[k]);
            }
        }
        if (best == SIZE_MAX) return;

        /* Find the separator (= or :) after the key, skipping whitespace
         * and optional quotes. If no separator within a few chars, treat
         * this hit as a false positive and continue scanning. */
        size_t sep = best + best_key_len;
        while (sep < len && (buf[sep] == '"' || buf[sep] == '\'' || buf[sep] == ' ' || buf[sep] == '\t')) {
            ++sep;
        }
        if (sep >= len || (buf[sep] != '=' && buf[sep] != ':')) {
            cursor = best + best_key_len;
            continue;
        }
        ++sep; /* step over = or : */
        /* Skip whitespace + opening quote. */
        while (sep < len && (buf[sep] == ' ' || buf[sep] == '\t' || buf[sep] == '"' || buf[sep] == '\'')) {
            ++sep;
        }
        /* Find end of value. */
        size_t end = sep;
        while (end < len && !is_value_terminator(buf[end])) {
            ++end;
        }
        if (end == sep) {
            cursor = sep;
            continue;
        }
        /* Replace [sep, end) with "***" -- shrink or grow as needed. */
        const char *rep = "***";
        size_t rep_len = 3;
        if (rep_len < end - sep) {
            size_t shrink = (end - sep) - rep_len;
            memmove(&buf[sep + rep_len], &buf[end], len - end + 1);
            memcpy(&buf[sep], rep, rep_len);
            len -= shrink;
        } else if (rep_len > end - sep) {
            size_t grow = rep_len - (end - sep);
            if (len + grow + 1 > buflen) {
                size_t cap = (buflen - 1) - sep;
                if (cap > rep_len) cap = rep_len;
                memcpy(&buf[sep], rep, cap);
                buf[sep + cap] = '\0';
                return;
            }
            memmove(&buf[sep + rep_len], &buf[end], len - end + 1);
            memcpy(&buf[sep], rep, rep_len);
            len += grow;
        } else {
            memcpy(&buf[sep], rep, rep_len);
        }
        cursor = sep + rep_len;
    }
}

void event_log_emit(event_level_t level,
                    const char *code,
                    const char *light_id,
                    const char *command_id,
                    const char *message)
{
    event_log_entry_t *e = &g_ring[g_head];
    memset(e, 0, sizeof *e);
    e->uptime_ms = now_ms();
    e->level = level;
    safe_copy(e->code,       sizeof e->code,       code);
    safe_copy(e->light_id,   sizeof e->light_id,   light_id);
    safe_copy(e->command_id, sizeof e->command_id, command_id);
    safe_copy(e->message,    sizeof e->message,    message);
    event_log_redact(e->message, sizeof e->message);

    g_head = (g_head + 1) % EVENT_LOG_CAPACITY;
    if (g_count < EVENT_LOG_CAPACITY) ++g_count;

#ifdef ESP_PLATFORM
    switch (level) {
        case EVENT_LEVEL_DEBUG: ESP_LOGD(TAG, "%s %s", e->code, e->message); break;
        case EVENT_LEVEL_INFO:  ESP_LOGI(TAG, "%s %s", e->code, e->message); break;
        case EVENT_LEVEL_WARN:  ESP_LOGW(TAG, "%s %s", e->code, e->message); break;
        case EVENT_LEVEL_ERROR: ESP_LOGE(TAG, "%s %s", e->code, e->message); break;
    }
#endif
}

size_t event_log_count(void)
{
    return g_count;
}

bool event_log_get(size_t idx, event_log_entry_t *out)
{
    if (!out || idx >= g_count) return false;
    size_t oldest = (g_count < EVENT_LOG_CAPACITY) ? 0 : g_head;
    size_t slot = (oldest + idx) % EVENT_LOG_CAPACITY;
    *out = g_ring[slot];
    return true;
}
