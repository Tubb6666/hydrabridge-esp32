#ifndef HYDRA_EVENT_LOG_H
#define HYDRA_EVENT_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bounded in-memory event ring. The ring is the source of truth for the
 * web UI's logs page and for the MQTT event topic. Important events are
 * mirrored to ESP_LOGI/W/E on the ESP-IDF build.
 *
 * Capacity is fixed at compile time (EVENT_LOG_CAPACITY). When the ring
 * is full, the oldest entry is overwritten.
 *
 * No dynamic allocation. Safe to call from any task context, but writes
 * are NOT reentrant -- caller must serialize concurrent emits if needed
 * (in v1, all emits happen from a small set of tasks, so a simple
 * critical section inside the ESP-IDF build is enough). */

#define EVENT_LOG_CAPACITY        150
#define EVENT_LOG_MSG_MAX          96
#define EVENT_LOG_CODE_MAX         24
#define EVENT_LOG_LIGHT_ID_MAX     32
#define EVENT_LOG_COMMAND_ID_MAX   32

typedef enum {
    EVENT_LEVEL_DEBUG = 0,
    EVENT_LEVEL_INFO  = 1,
    EVENT_LEVEL_WARN  = 2,
    EVENT_LEVEL_ERROR = 3,
} event_level_t;

typedef struct {
    uint64_t      uptime_ms;
    event_level_t level;
    char          code[EVENT_LOG_CODE_MAX];               /* structured code, e.g. "connect_timeout" */
    char          light_id[EVENT_LOG_LIGHT_ID_MAX];       /* "" if not light-specific */
    char          command_id[EVENT_LOG_COMMAND_ID_MAX];   /* "" if not command-specific */
    char          message[EVENT_LOG_MSG_MAX];             /* short human message, redacted */
} event_log_entry_t;

void event_log_init(void);

/* Emit one event. message is copied with redaction applied (any
 * substring resembling password=, password:, "password":"..." has its
 * value replaced with "***" before storage). code/light_id/command_id
 * are copied verbatim, truncated to their max size if needed. NULL
 * pointers are treated as empty strings.
 *
 * On the ESP-IDF build this also calls ESP_LOG[D|I|W|E] on tag
 * "event_log" so events show up in serial monitor without extra setup. */
void event_log_emit(event_level_t level,
                    const char *code,
                    const char *light_id,
                    const char *command_id,
                    const char *message);

/* Number of entries currently stored (0..EVENT_LOG_CAPACITY). */
size_t event_log_count(void);

/* Copy the entry at logical position idx (0 = oldest) into *out.
 * Returns false if idx is out of range. */
bool event_log_get(size_t idx, event_log_entry_t *out);

/* For tests / factory reset: drop every entry. */
void event_log_reset(void);

/* In-place redaction. Public for tests. Replaces the value following any
 * substring "password", "passwd", "pwd", "secret" (case-insensitive,
 * followed by '=' or ':' optionally with whitespace/quotes) with "***".
 * Stops at whitespace, comma, quote, brace, or NUL. */
void event_log_redact(char *buf, size_t buflen);

/* Optional clock override (test hook). Pass NULL to restore the default
 * clock (esp_timer on ESP-IDF, 0 on host). The function should return
 * milliseconds since boot. */
void event_log_set_clock_fn(uint64_t (*fn)(void));

#endif /* HYDRA_EVENT_LOG_H */
