#include "command_queue.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
static const char *TAG = "command_queue";
#endif

typedef struct {
    char              light_id[LIGHT_ID_LEN];
    pending_command_t entries[CMD_QUEUE_DEPTH];
    uint8_t           head;
    uint8_t           count;
    bool              in_use;
} per_light_queue_t;

static per_light_queue_t g_queues[LIGHT_REGISTRY_CAPACITY];
static uint64_t (*g_clock_fn)(void) = NULL;

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

void cmd_queue_set_clock_fn(uint64_t (*fn)(void)) { g_clock_fn = fn; }

void cmd_queue_reset(void)
{
    memset(g_queues, 0, sizeof g_queues);
}

static per_light_queue_t *find_queue(const char *light_id, bool create)
{
    if (!light_id) return NULL;
    for (size_t i = 0; i < LIGHT_REGISTRY_CAPACITY; ++i) {
        if (g_queues[i].in_use &&
            strncmp(g_queues[i].light_id, light_id, LIGHT_ID_LEN) == 0) {
            return &g_queues[i];
        }
    }
    if (!create) return NULL;
    for (size_t i = 0; i < LIGHT_REGISTRY_CAPACITY; ++i) {
        if (!g_queues[i].in_use) {
            memset(&g_queues[i], 0, sizeof g_queues[i]);
            strncpy(g_queues[i].light_id, light_id, LIGHT_ID_LEN - 1);
            g_queues[i].light_id[LIGHT_ID_LEN - 1] = '\0';
            g_queues[i].in_use = true;
            return &g_queues[i];
        }
    }
    return NULL;
}

static uint8_t ring_idx(uint8_t base, uint8_t offset)
{
    return (uint8_t)((base + offset) % CMD_QUEUE_DEPTH);
}

int cmd_queue_push(const pending_command_t *cmd)
{
    if (!cmd) return -1;
    if (cmd->light_id[0] == '\0') return -1;

    per_light_queue_t *q = find_queue(cmd->light_id, true);
    if (!q) return -1;

    if (cmd->type == CMD_TYPE_SET_CHANNELS) {
        for (uint8_t i = 0; i < q->count; ++i) {
            uint8_t slot = ring_idx(q->head, i);
            if (q->entries[slot].type == CMD_TYPE_SET_CHANNELS) {
                q->entries[slot] = *cmd;
                q->entries[slot].enqueue_ms = now_ms();
                return 0;
            }
        }
    }

    if (q->count >= CMD_QUEUE_DEPTH) return -1;

    uint8_t tail = ring_idx(q->head, q->count);
    q->entries[tail] = *cmd;
    q->entries[tail].enqueue_ms = now_ms();
    ++q->count;
    return 0;
}

int cmd_queue_pop(const char *light_id, pending_command_t *out)
{
    if (!out) return -1;
    per_light_queue_t *q = find_queue(light_id, false);
    if (!q || q->count == 0) return -1;

    *out = q->entries[q->head];
    memset(&q->entries[q->head], 0, sizeof q->entries[q->head]);
    q->head = ring_idx(q->head, 1);
    --q->count;
    if (q->count == 0) {
        memset(q, 0, sizeof *q);
    }
    return 0;
}

int cmd_queue_peek(const char *light_id, pending_command_t *out)
{
    if (!out) return -1;
    per_light_queue_t *q = find_queue(light_id, false);
    if (!q || q->count == 0) return -1;
    *out = q->entries[q->head];
    return 0;
}

size_t cmd_queue_depth(const char *light_id)
{
    per_light_queue_t *q = find_queue(light_id, false);
    return q ? q->count : 0;
}

size_t cmd_queue_expire(void)
{
    uint64_t t = now_ms();
    size_t dropped = 0;
    for (size_t i = 0; i < LIGHT_REGISTRY_CAPACITY; ++i) {
        per_light_queue_t *q = &g_queues[i];
        if (!q->in_use) continue;
        while (q->count > 0) {
            const pending_command_t *e = &q->entries[q->head];
            if (e->timeout_ms == 0) break;
            if (e->enqueue_ms + e->timeout_ms > t) break;
            memset(&q->entries[q->head], 0, sizeof q->entries[q->head]);
            q->head = ring_idx(q->head, 1);
            --q->count;
            ++dropped;
        }
        if (q->count == 0) {
            memset(q, 0, sizeof *q);
        }
    }
    return dropped;
}

#ifdef ESP_PLATFORM
esp_err_t cmd_queue_init(void)
{
    cmd_queue_reset();
    ESP_LOGI(TAG, "init (%d lights x %d depth)",
             LIGHT_REGISTRY_CAPACITY, CMD_QUEUE_DEPTH);
    return ESP_OK;
}
#endif
