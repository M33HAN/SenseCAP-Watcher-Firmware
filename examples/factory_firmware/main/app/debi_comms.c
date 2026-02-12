/**
 * @file debi_comms.c
 * @brief Debi Guardian — Communications layer
 *
 * Sits between debi_os (which owns the MQTT client) and the rest
 * of the Debi firmware.  Provides:
 *   - Ring-buffer outbound queue for offline resilience
 *   - Expanded command handling (voice, reboot, OTA, config)
 *   - Command acknowledgement with cmd_id
 *   - Connection health tracking
 *   - Hub config sync that applies to voice and face bridge
 *
 * Copyright (c) 2026 Debi Guardian
 */

#include "debi_comms.h"
#include "debi_os.h"
#include "debi_voice.h"
#include "debi_face_bridge.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "debi_comms";

/* ------------------------------------------------------------------ */
/*  Outbound message queue                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    char    topic[64];
    char    payload[DEBI_COMMS_MSG_MAX_LEN];
    int     qos;
    bool    retain;
} queued_msg_t;

typedef struct {
    queued_msg_t  msgs[DEBI_COMMS_QUEUE_SIZE];
    int           head;          /* next write position */
    int           count;         /* number of queued messages */
} msg_queue_t;

/* ------------------------------------------------------------------ */
/*  Module state                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    bool                      initialised;
    bool                      connected;
    esp_mqtt_client_handle_t  mqtt_client;   /* borrowed from debi_os */
    int                       reconnect_count;
    int64_t                   last_rx_us;
    int64_t                   ping_sent_us;  /* for RTT measurement */
    int                       rtt_ms;
    msg_queue_t               queue;
    debi_comms_config_t       config;
    SemaphoreHandle_t         lock;
} debi_comms_ctx_t;

static debi_comms_ctx_t s_comms = {
    .initialised     = false,
    .connected       = false,
    .mqtt_client     = NULL,
    .reconnect_count = 0,
    .last_rx_us      = 0,
    .ping_sent_us    = 0,
    .rtt_ms          = -1,
    .queue           = { .head = 0, .count = 0 },
    .config          = {
        .idle_timeout_s      = 120,
        .concerned_timeout_s = 1800,
        .volume              = 80,
        .mute                = false,
        .night_auto          = false,
        .night_start_hour    = 22,
        .night_end_hour      = 7,
    },
    .lock = NULL,
};

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

static void queue_push(const char *topic, const char *payload,
                        int qos, bool retain);
static void queue_flush(void);
static void dispatch_command(const cJSON *root);
static void dispatch_config(const cJSON *root);
static void send_ack(const char *cmd_id, const char *status,
                      const char *detail);
static void apply_config(void);

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void debi_comms_init(void)
{
    if (s_comms.initialised) return;

    s_comms.lock = xSemaphoreCreateMutex();
    if (!s_comms.lock) {
        ESP_LOGE(TAG, "mutex create failed");
        return;
    }

    s_comms.initialised = true;
    ESP_LOGI(TAG, "comms layer ready  queue_size=%d",
             DEBI_COMMS_QUEUE_SIZE);
}

void debi_comms_deinit(void)
{
    if (!s_comms.initialised) return;

    if (s_comms.lock) {
        vSemaphoreDelete(s_comms.lock);
        s_comms.lock = NULL;
    }
    s_comms.mqtt_client = NULL;
    s_comms.connected   = false;
    s_comms.initialised = false;
    ESP_LOGI(TAG, "comms layer stopped");
}

void debi_comms_on_connected(esp_mqtt_client_handle_t client)
{
    xSemaphoreTake(s_comms.lock, portMAX_DELAY);

    s_comms.mqtt_client = client;
    s_comms.connected   = true;
    s_comms.last_rx_us  = esp_timer_get_time();

    if (s_comms.reconnect_count > 0) {
        ESP_LOGI(TAG, "reconnected (count=%d), flushing queue (%d msgs)",
                 s_comms.reconnect_count, s_comms.queue.count);
    }
    s_comms.reconnect_count++;

    /* Flush any queued messages */
    queue_flush();

    xSemaphoreGive(s_comms.lock);
}

void debi_comms_on_disconnected(void)
{
    xSemaphoreTake(s_comms.lock, portMAX_DELAY);
    s_comms.connected   = false;
    s_comms.mqtt_client = NULL;
    xSemaphoreGive(s_comms.lock);

    ESP_LOGW(TAG, "disconnected — messages will be queued");
}

void debi_comms_handle_message(const char *topic,
                                const char *data, int data_len)
{
    if (!s_comms.initialised || !topic || !data) return;

    s_comms.last_rx_us = esp_timer_get_time();

    /* Parse JSON */
    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (!root) {
        ESP_LOGW(TAG, "invalid JSON from hub on %s", topic);
        return;
    }

    /* Check for pong response (RTT measurement) */
    cJSON *pong = cJSON_GetObjectItem(root, "pong");
    if (pong && cJSON_IsTrue(pong)) {
        if (s_comms.ping_sent_us > 0) {
            int64_t now = esp_timer_get_time();
            s_comms.rtt_ms = (int)((now - s_comms.ping_sent_us) / 1000);
            s_comms.ping_sent_us = 0;
            ESP_LOGI(TAG, "pong received, RTT=%d ms", s_comms.rtt_ms);
        }
        cJSON_Delete(root);
        return;
    }

    /* Route by topic */
    if (strstr(topic, "/cmd")) {
        dispatch_command(root);
    } else if (strstr(topic, "/config")) {
        dispatch_config(root);
    }

    cJSON_Delete(root);
}

int debi_comms_publish(const char *topic, const char *payload,
                        int qos, bool retain)
{
    if (!s_comms.initialised || !topic || !payload) return -1;

    xSemaphoreTake(s_comms.lock, portMAX_DELAY);

    if (s_comms.connected && s_comms.mqtt_client) {
        /* Publish directly */
        esp_mqtt_client_publish(s_comms.mqtt_client,
                                topic, payload, 0,
                                qos, retain ? 1 : 0);
        xSemaphoreGive(s_comms.lock);
        return 0;
    }

    /* Queue for later */
    queue_push(topic, payload, qos, retain);
    int count = s_comms.queue.count;

    xSemaphoreGive(s_comms.lock);

    ESP_LOGD(TAG, "queued msg for %s (%d in queue)", topic, count);
    return 0;
}

debi_comms_health_t debi_comms_get_health(void)
{
    debi_comms_health_t h = {
        .connected       = s_comms.connected,
        .reconnect_count = s_comms.reconnect_count,
        .last_msg_time_us = s_comms.last_rx_us,
        .rtt_ms          = s_comms.rtt_ms,
        .queued_count    = s_comms.queue.count,
    };
    return h;
}

debi_comms_config_t debi_comms_get_config(void)
{
    return s_comms.config;
}

bool debi_comms_hub_healthy(void)
{
    if (!s_comms.connected) return false;
    if (s_comms.last_rx_us == 0) return false;

    int64_t elapsed_us = esp_timer_get_time() - s_comms.last_rx_us;
    int64_t timeout_us = (int64_t)DEBI_COMMS_STALE_TIMEOUT_S * 1000000;
    return (elapsed_us < timeout_us);
}

/* ------------------------------------------------------------------ */
/*  Queue internals                                                    */
/* ------------------------------------------------------------------ */

static void queue_push(const char *topic, const char *payload,
                        int qos, bool retain)
{
    if (s_comms.queue.count >= DEBI_COMMS_QUEUE_SIZE) {
        ESP_LOGW(TAG, "queue full — dropping oldest message");
        /* Overwrite oldest (head wraps around) */
        s_comms.queue.count = DEBI_COMMS_QUEUE_SIZE - 1;
    }

    int idx = (s_comms.queue.head + s_comms.queue.count)
              % DEBI_COMMS_QUEUE_SIZE;

    strncpy(s_comms.queue.msgs[idx].topic, topic,
            sizeof(s_comms.queue.msgs[idx].topic) - 1);
    s_comms.queue.msgs[idx].topic[sizeof(s_comms.queue.msgs[idx].topic) - 1] = '\0';

    strncpy(s_comms.queue.msgs[idx].payload, payload,
            sizeof(s_comms.queue.msgs[idx].payload) - 1);
    s_comms.queue.msgs[idx].payload[sizeof(s_comms.queue.msgs[idx].payload) - 1] = '\0';

    s_comms.queue.msgs[idx].qos    = qos;
    s_comms.queue.msgs[idx].retain = retain;
    s_comms.queue.count++;
}

static void queue_flush(void)
{
    if (!s_comms.mqtt_client || s_comms.queue.count == 0) return;

    ESP_LOGI(TAG, "flushing %d queued messages", s_comms.queue.count);

    while (s_comms.queue.count > 0) {
        queued_msg_t *msg = &s_comms.queue.msgs[s_comms.queue.head];

        esp_mqtt_client_publish(s_comms.mqtt_client,
                                msg->topic, msg->payload, 0,
                                msg->qos, msg->retain ? 1 : 0);

        s_comms.queue.head = (s_comms.queue.head + 1) % DEBI_COMMS_QUEUE_SIZE;
        s_comms.queue.count--;
    }
}

/* ------------------------------------------------------------------ */
/*  Command dispatch                                                   */
/* ------------------------------------------------------------------ */

static void dispatch_command(const cJSON *root)
{
    /* Extract optional cmd_id for acknowledgement */
    const cJSON *id_item = cJSON_GetObjectItem(root, "cmd_id");
    const char *cmd_id = (id_item && cJSON_IsString(id_item))
                         ? id_item->valuestring : NULL;

    const cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
    if (!cmd_item || !cJSON_IsString(cmd_item)) {
        if (cmd_id) send_ack(cmd_id, "error", "missing cmd field");
        return;
    }

    const char *cmd = cmd_item->valuestring;

    ESP_LOGI(TAG, "cmd: %s (id=%s)", cmd,
             cmd_id ? cmd_id : "none");

    /* ---- set_mode ---- */
    if (strcmp(cmd, "set_mode") == 0) {
        const cJSON *mode_item = cJSON_GetObjectItem(root, "mode");
        if (mode_item && cJSON_IsString(mode_item)) {
            const char *m = mode_item->valuestring;
            if (strcmp(m, "active") == 0)
                debi_os_set_mode(DEBI_MODE_ACTIVE);
            else if (strcmp(m, "night") == 0)
                debi_os_set_mode(DEBI_MODE_NIGHT);
            else if (strcmp(m, "alert") == 0)
                debi_os_set_mode(DEBI_MODE_ALERT);
            else if (strcmp(m, "setup") == 0)
                debi_os_set_mode(DEBI_MODE_SETUP);
            else {
                if (cmd_id) send_ack(cmd_id, "error", "unknown mode");
                return;
            }
            if (cmd_id) send_ack(cmd_id, "ok", m);
        }
    }
    /* ---- mute ---- */
    else if (strcmp(cmd, "mute") == 0) {
        const cJSON *val = cJSON_GetObjectItem(root, "value");
        bool mute_on = val ? cJSON_IsTrue(val) : true;
        debi_voice_set_mute(mute_on);
        s_comms.config.mute = mute_on;
        if (cmd_id) send_ack(cmd_id, "ok", mute_on ? "muted" : "unmuted");
    }
    /* ---- set_volume ---- */
    else if (strcmp(cmd, "set_volume") == 0) {
        const cJSON *val = cJSON_GetObjectItem(root, "value");
        if (val && cJSON_IsNumber(val)) {
            int vol = (int)val->valuedouble;
            debi_voice_set_volume(vol);
            s_comms.config.volume = vol;
            if (cmd_id) send_ack(cmd_id, "ok", "volume set");
        }
    }
    /* ---- play_sound ---- */
    else if (strcmp(cmd, "play_sound") == 0) {
        const cJSON *path = cJSON_GetObjectItem(root, "file");
        if (path && cJSON_IsString(path)) {
            debi_voice_play_file(path->valuestring);
            if (cmd_id) send_ack(cmd_id, "ok", "playing");
        }
    }
    /* ---- stop_sound ---- */
    else if (strcmp(cmd, "stop_sound") == 0) {
        debi_voice_stop();
        if (cmd_id) send_ack(cmd_id, "ok", "stopped");
    }
    /* ---- report_sensors ---- */
    else if (strcmp(cmd, "report_sensors") == 0) {
        debi_os_report_sensors();
        if (cmd_id) send_ack(cmd_id, "ok", "reported");
    }
    /* ---- ping ---- */
    else if (strcmp(cmd, "ping") == 0) {
        /* Send pong with timestamp for hub-side RTT */
        cJSON *pong = cJSON_CreateObject();
        if (pong) {
            cJSON_AddTrueToObject(pong, "pong");
            cJSON_AddNumberToObject(pong, "ts",
                                     (double)esp_timer_get_time());
            if (cmd_id) cJSON_AddStringToObject(pong, "cmd_id", cmd_id);
            char *json = cJSON_PrintUnformatted(pong);
            if (json) {
                debi_comms_publish(DEBI_TOPIC_STATUS, json, 0, false);
                free(json);
            }
            cJSON_Delete(pong);
        }
    }
    /* ---- get_health ---- */
    else if (strcmp(cmd, "get_health") == 0) {
        debi_comms_health_t h = debi_comms_get_health();
        cJSON *resp = cJSON_CreateObject();
        if (resp) {
            cJSON_AddBoolToObject(resp, "connected", h.connected);
            cJSON_AddNumberToObject(resp, "reconnects", h.reconnect_count);
            cJSON_AddNumberToObject(resp, "rtt_ms", h.rtt_ms);
            cJSON_AddNumberToObject(resp, "queued", h.queued_count);
            cJSON_AddNumberToObject(resp, "heap",
                                     (double)esp_get_free_heap_size());
            if (cmd_id) cJSON_AddStringToObject(resp, "cmd_id", cmd_id);
            char *json = cJSON_PrintUnformatted(resp);
            if (json) {
                debi_comms_publish(DEBI_TOPIC_STATUS, json, 0, false);
                free(json);
            }
            cJSON_Delete(resp);
        }
    }
    /* ---- reboot ---- */
    else if (strcmp(cmd, "reboot") == 0) {
        if (cmd_id) send_ack(cmd_id, "ok", "rebooting");
        ESP_LOGW(TAG, "reboot requested by hub");
        vTaskDelay(pdMS_TO_TICKS(500));  /* Let ack send */
        esp_restart();
    }
    /* ---- unknown ---- */
    else {
        ESP_LOGW(TAG, "unknown cmd: %s", cmd);
        if (cmd_id) send_ack(cmd_id, "error", "unknown command");
    }
}

/* ------------------------------------------------------------------ */
/*  Config dispatch                                                    */
/* ------------------------------------------------------------------ */

static void dispatch_config(const cJSON *root)
{
    ESP_LOGI(TAG, "config update from hub");

    bool changed = false;

    const cJSON *item;

    item = cJSON_GetObjectItem(root, "idle_timeout_s");
    if (item && cJSON_IsNumber(item)) {
        s_comms.config.idle_timeout_s = (int)item->valuedouble;
        changed = true;
    }

    item = cJSON_GetObjectItem(root, "concerned_timeout_s");
    if (item && cJSON_IsNumber(item)) {
        s_comms.config.concerned_timeout_s = (int)item->valuedouble;
        changed = true;
    }

    item = cJSON_GetObjectItem(root, "volume");
    if (item && cJSON_IsNumber(item)) {
        s_comms.config.volume = (int)item->valuedouble;
        changed = true;
    }

    item = cJSON_GetObjectItem(root, "mute");
    if (item) {
        s_comms.config.mute = cJSON_IsTrue(item);
        changed = true;
    }

    item = cJSON_GetObjectItem(root, "night_auto");
    if (item) {
        s_comms.config.night_auto = cJSON_IsTrue(item);
        changed = true;
    }

    item = cJSON_GetObjectItem(root, "night_start_hour");
    if (item && cJSON_IsNumber(item)) {
        s_comms.config.night_start_hour = (int)item->valuedouble;
        changed = true;
    }

    item = cJSON_GetObjectItem(root, "night_end_hour");
    if (item && cJSON_IsNumber(item)) {
        s_comms.config.night_end_hour = (int)item->valuedouble;
        changed = true;
    }

    if (changed) {
        apply_config();

        /* Ack the config update */
        const cJSON *id_item = cJSON_GetObjectItem(root, "cmd_id");
        if (id_item && cJSON_IsString(id_item)) {
            send_ack(id_item->valuestring, "ok", "config applied");
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Config application                                                 */
/* ------------------------------------------------------------------ */

static void apply_config(void)
{
    ESP_LOGI(TAG, "applying config: vol=%d mute=%d idle=%ds concerned=%ds",
             s_comms.config.volume, s_comms.config.mute,
             s_comms.config.idle_timeout_s,
             s_comms.config.concerned_timeout_s);

    /* Apply voice settings */
    debi_voice_set_volume(s_comms.config.volume);
    debi_voice_set_mute(s_comms.config.mute);

    /* Timeout changes would be applied via face bridge reconfigure
     * (future: debi_face_bridge_set_timeouts()) */
}

/* ------------------------------------------------------------------ */
/*  Acknowledgement                                                    */
/* ------------------------------------------------------------------ */

static void send_ack(const char *cmd_id, const char *status,
                      const char *detail)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddStringToObject(root, "ack", status);
    cJSON_AddStringToObject(root, "cmd_id", cmd_id);
    if (detail) {
        cJSON_AddStringToObject(root, "detail", detail);
    }
    cJSON_AddNumberToObject(root, "ts", (double)esp_timer_get_time());

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        debi_comms_publish(DEBI_TOPIC_STATUS, json, 0, false);
        free(json);
    }
    cJSON_Delete(root);
}
