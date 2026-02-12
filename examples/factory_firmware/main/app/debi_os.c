/**
 * @file debi_os.c
 * @brief Debi OS -- Core State Machine & Hub Connection
 *
 * Manages the device lifecycle, connects to the Debi Guardian
 * Pi hub via MQTT, and bridges sensor/detection data.
 *
 * Copyright (c) 2026 Debi Guardian
 */

#include "debi_os.h"

#include <string.h>
#include <time.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "cJSON.h"

#include "event_loops.h"
#include "data_defs.h"
#include "app_sensor.h"
#include "debi_face_bridge.h"
#include "view/ui_face_states.h"
#include "debi_comms.h"

static const char *TAG = "debi_os";

/* ---- Mode names ---- */
static const char *MODE_NAMES[] = {
    [DEBI_MODE_BOOT]       = "Boot",
    [DEBI_MODE_CONNECTING] = "Connecting",
    [DEBI_MODE_ACTIVE]     = "Active",
    [DEBI_MODE_NIGHT]      = "Night",
    [DEBI_MODE_ALERT]      = "Alert",
    [DEBI_MODE_SETUP]      = "Setup",
    [DEBI_MODE_ERROR]      = "Error",
};

/* ---- Internal state ---- */
typedef struct {
    debi_mode_t             mode;
    bool                    hub_connected;
    esp_mqtt_client_handle_t mqtt_handle;
    esp_timer_handle_t      heartbeat_timer;
    esp_timer_handle_t      sensor_timer;
    time_t                  boot_time;
    uint32_t                detection_count;
    uint32_t                heartbeat_seq;
} debi_os_state_t;

static debi_os_state_t s_os = {
    .mode            = DEBI_MODE_BOOT,
    .hub_connected   = false,
    .mqtt_handle     = NULL,
    .heartbeat_timer = NULL,
    .sensor_timer    = NULL,
    .boot_time       = 0,
    .detection_count = 0,
    .heartbeat_seq   = 0,
};

/* ---- Forward declarations ---- */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data);
static void on_wifi_connected(void *handler_arg, esp_event_base_t base,
                               int32_t id, void *event_data);
static void on_wifi_disconnected(void *handler_arg, esp_event_base_t base,
                                  int32_t id, void *event_data);
static void on_detection_event(void *handler_arg, esp_event_base_t base,
                                int32_t id, void *event_data);
static void heartbeat_cb(void *arg);
static void sensor_report_cb(void *arg);
static void mqtt_connect(void);
static void mqtt_disconnect(void);
static void publish_status(void);
static void handle_hub_command(const char *data, int len);

/* ============================================================
 *  Public API
 * ============================================================ */

void debi_os_init(void)
{
    ESP_LOGI(TAG, "===================================");
    ESP_LOGI(TAG, "  Debi Guardian OS initialising");
    ESP_LOGI(TAG, "  Hub: %s", DEBI_HUB_MQTT_URI);
    ESP_LOGI(TAG, "  Client: %s", DEBI_HUB_MQTT_CLIENT_ID);
    ESP_LOGI(TAG, "===================================");

    s_os.mode            = DEBI_MODE_BOOT;
    s_os.boot_time       = time(NULL);
    s_os.hub_connected   = false;
    s_os.detection_count = 0;
    s_os.heartbeat_seq   = 0;

    /* Listen for WiFi connect/disconnect via ctrl events */
    esp_event_handler_register_with(
        app_event_loop_handle, CTRL_EVENT_BASE,
        CTRL_EVENT_MQTT_CONNECTED, on_wifi_connected, NULL);
    esp_event_handler_register_with(
        app_event_loop_handle, CTRL_EVENT_BASE,
        CTRL_EVENT_MQTT_DISCONNECTED, on_wifi_disconnected, NULL);

    /* Listen for local detection events to relay to hub */
    esp_event_handler_register_with(
        app_event_loop_handle, VIEW_EVENT_BASE,
        VIEW_EVENT_TASK_FLOW_START_BY_LOCAL, on_detection_event, NULL);

    /* Create heartbeat timer */
    const esp_timer_create_args_t hb_args = {
        .callback = heartbeat_cb,
        .arg      = NULL,
        .name     = "debi_heartbeat",
    };
    ESP_ERROR_CHECK(esp_timer_create(&hb_args, &s_os.heartbeat_timer));

    /* Create sensor report timer */
    const esp_timer_create_args_t sr_args = {
        .callback = sensor_report_cb,
        .arg      = NULL,
        .name     = "debi_sensor_report",
    };
    ESP_ERROR_CHECK(esp_timer_create(&sr_args, &s_os.sensor_timer));

    /* Start MQTT connection attempt */
    s_os.mode = DEBI_MODE_CONNECTING;
    ui_face_set_state(FACE_STATE_BOOT);
    mqtt_connect();

    ESP_LOGI(TAG, "Debi OS ready, mode=%s", MODE_NAMES[s_os.mode]);
}

void debi_os_deinit(void)
{
    if (s_os.heartbeat_timer) {
        esp_timer_stop(s_os.heartbeat_timer);
        esp_timer_delete(s_os.heartbeat_timer);
        s_os.heartbeat_timer = NULL;
    }
    if (s_os.sensor_timer) {
        esp_timer_stop(s_os.sensor_timer);
        esp_timer_delete(s_os.sensor_timer);
        s_os.sensor_timer = NULL;
    }
    mqtt_disconnect();
    s_os.mode = DEBI_MODE_BOOT;
    ESP_LOGI(TAG, "Debi OS shut down");
}

debi_mode_t debi_os_get_mode(void)
{
    return s_os.mode;
}

void debi_os_set_mode(debi_mode_t mode)
{
    if (mode >= DEBI_MODE_COUNT) return;
    if (mode == s_os.mode) return;

    debi_mode_t old = s_os.mode;
    s_os.mode = mode;

    ESP_LOGI(TAG, "mode: %s -> %s", MODE_NAMES[old], MODE_NAMES[mode]);

    /* Update face to match mode */
    switch (mode) {
        case DEBI_MODE_ACTIVE:
            /* Let face bridge handle it automatically */
            break;
        case DEBI_MODE_NIGHT:
            debi_face_bridge_override(FACE_STATE_NIGHT);
            break;
        case DEBI_MODE_ALERT:
            debi_face_bridge_override(FACE_STATE_ALERT_FALL);
            break;
        case DEBI_MODE_SETUP:
            debi_face_bridge_override(FACE_STATE_SETUP);
            break;
        case DEBI_MODE_ERROR:
            debi_face_bridge_override(FACE_STATE_ERROR);
            break;
        case DEBI_MODE_CONNECTING:
            debi_face_bridge_override(FACE_STATE_BOOT);
            break;
        default:
            break;
    }

    /* Publish mode change to hub */
    publish_status();
}

const char *debi_os_mode_name(debi_mode_t mode)
{
    if (mode >= DEBI_MODE_COUNT) return "Unknown";
    return MODE_NAMES[mode];
}

bool debi_os_hub_connected(void)
{
    return s_os.hub_connected;
}

void debi_os_report_detection(const char *type, int score)
{
    if (!s_os.hub_connected || !s_os.mqtt_handle) return;

    s_os.detection_count++;

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddStringToObject(root, "type", type ? type : "unknown");
    cJSON_AddNumberToObject(root, "score", score);
    cJSON_AddNumberToObject(root, "seq", s_os.detection_count);
    cJSON_AddStringToObject(root, "mode", MODE_NAMES[s_os.mode]);
    cJSON_AddNumberToObject(root, "ts", (double)time(NULL));

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        esp_mqtt_client_publish(s_os.mqtt_handle,
                                 DEBI_TOPIC_DETECTION, json, 0, 0, 0);
        free(json);
    }
    cJSON_Delete(root);
}

void debi_os_report_sensors(void)
{
    if (!s_os.hub_connected || !s_os.mqtt_handle) return;

    app_sensor_data_t sensor_data[APP_SENSOR_SUPPORT_MAX];
    uint8_t count = app_sensor_read_measurement(sensor_data,
                                                  APP_SENSOR_SUPPORT_MAX);

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddNumberToObject(root, "ts", (double)time(NULL));

    for (uint8_t i = 0; i < count; i++) {
        if (sensor_data[i].type == SENSOR_SHT4x) {
            cJSON_AddNumberToObject(root, "temp_c",
                sensor_data[i].context.sht4x.temperature / 100.0);
            cJSON_AddNumberToObject(root, "humidity",
                sensor_data[i].context.sht4x.humidity / 100.0);
        } else if (sensor_data[i].type == SENSOR_SCD4x) {
            cJSON_AddNumberToObject(root, "temp_c",
                sensor_data[i].context.scd4x.temperature / 100.0);
            cJSON_AddNumberToObject(root, "humidity",
                sensor_data[i].context.scd4x.humidity / 100.0);
            cJSON_AddNumberToObject(root, "co2",
                sensor_data[i].context.scd4x.co2);
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        esp_mqtt_client_publish(s_os.mqtt_handle,
                                 DEBI_TOPIC_SENSOR, json, 0, 0, 0);
        free(json);
    }
    cJSON_Delete(root);
}

/* ============================================================
 *  MQTT connection management
 * ============================================================ */

static void mqtt_connect(void)
{
    if (s_os.mqtt_handle) return; /* already connected or connecting */

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri         = DEBI_HUB_MQTT_URI,
        .credentials.client_id      = DEBI_HUB_MQTT_CLIENT_ID,
        .session.disable_clean_session = false,
        .network.disable_auto_reconnect = false,
        .network.reconnect_timeout_ms   = 10000,
    };

    s_os.mqtt_handle = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_os.mqtt_handle) {
        ESP_LOGE(TAG, "MQTT client init failed");
        debi_os_set_mode(DEBI_MODE_ERROR);
        return;
    }

    esp_mqtt_client_register_event(s_os.mqtt_handle,
                                    ESP_EVENT_ANY_ID,
                                    mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_os.mqtt_handle);
    ESP_LOGI(TAG, "MQTT connecting to %s", DEBI_HUB_MQTT_URI);
}

static void mqtt_disconnect(void)
{
    if (!s_os.mqtt_handle) return;

    /* Publish offline status before disconnecting */
    if (s_os.hub_connected) {
        cJSON *root = cJSON_CreateObject();
        if (root) {
            cJSON_AddStringToObject(root, "status", "offline");
            cJSON_AddNumberToObject(root, "ts", (double)time(NULL));
            char *json = cJSON_PrintUnformatted(root);
            if (json) {
                esp_mqtt_client_publish(s_os.mqtt_handle,
                    DEBI_TOPIC_STATUS, json, 0, 1, 1);
                free(json);
            }
            cJSON_Delete(root);
        }
    }

    esp_mqtt_client_stop(s_os.mqtt_handle);
    esp_mqtt_client_destroy(s_os.mqtt_handle);
    s_os.mqtt_handle = NULL;
    s_os.hub_connected = false;
        debi_comms_on_disconnected();
}

/* ============================================================
 *  MQTT event handler
 * ============================================================ */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Hub MQTT connected!");
            s_os.hub_connected = true;

            /* Subscribe to command topic */
            esp_mqtt_client_subscribe(s_os.mqtt_handle,
                                       DEBI_TOPIC_CMD, 0);
            esp_mqtt_client_subscribe(s_os.mqtt_handle,
                                       DEBI_TOPIC_CONFIG, 0);

            /* Publish online status (retained) */
            publish_status();
        debi_comms_on_connected(s_os.mqtt_handle);

            /* Start timers */
            esp_timer_start_periodic(s_os.heartbeat_timer,
                (uint64_t)DEBI_HEARTBEAT_INTERVAL_S * 1000000);
            esp_timer_start_periodic(s_os.sensor_timer,
                (uint64_t)DEBI_SENSOR_REPORT_INTERVAL_S * 1000000);

            /* Transition to active mode */
            if (s_os.mode == DEBI_MODE_CONNECTING ||
                s_os.mode == DEBI_MODE_BOOT) {
                debi_os_set_mode(DEBI_MODE_ACTIVE);
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Hub MQTT disconnected");
            s_os.hub_connected = false;
        debi_comms_on_disconnected();

            /* Stop periodic timers */
            esp_timer_stop(s_os.heartbeat_timer);
            esp_timer_stop(s_os.sensor_timer);

            if (s_os.mode == DEBI_MODE_ACTIVE) {
                s_os.mode = DEBI_MODE_CONNECTING;
                ESP_LOGW(TAG, "mode -> Connecting (will auto-reconnect)");
            }
            break;

        case MQTT_EVENT_DATA: {
            /* Incoming message from hub */
            if (!event->topic || !event->data) break;

            /* Null-terminate for safe string ops */
            char topic[128];
            int tlen = event->topic_len < 127 ? event->topic_len : 127;
            memcpy(topic, event->topic, tlen);
            topic[tlen] = '\0';

            ESP_LOGI(TAG, "hub msg: %s (%d bytes)", topic, event->data_len);

            if (strstr(topic, "/cmd") || strstr(topic, "/config")) {
                handle_hub_command(event->data, event->data_len);
            debi_comms_handle_message(topic, event->data, event->data_len);
            }
            break;
        }

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;

        default:
            break;
    }
}

/* ============================================================
 *  Hub command handler
 * ============================================================ */

static void handle_hub_command(const char *data, int len)
{
    /* Parse JSON command from hub */
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "invalid hub command JSON");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (cmd && cJSON_IsString(cmd)) {
        const char *c = cmd->valuestring;

        if (strcmp(c, "set_mode") == 0) {
            cJSON *mode_val = cJSON_GetObjectItem(root, "mode");
            if (mode_val && cJSON_IsString(mode_val)) {
                const char *m = mode_val->valuestring;
                if (strcmp(m, "active") == 0)
                    debi_os_set_mode(DEBI_MODE_ACTIVE);
                else if (strcmp(m, "night") == 0)
                    debi_os_set_mode(DEBI_MODE_NIGHT);
                else if (strcmp(m, "alert") == 0)
                    debi_os_set_mode(DEBI_MODE_ALERT);
                else if (strcmp(m, "setup") == 0)
                    debi_os_set_mode(DEBI_MODE_SETUP);
                else
                    ESP_LOGW(TAG, "unknown mode: %s", m);
            }
        } else if (strcmp(c, "report_sensors") == 0) {
            debi_os_report_sensors();
        } else if (strcmp(c, "ping") == 0) {
            /* Respond with pong */
            if (s_os.mqtt_handle) {
                esp_mqtt_client_publish(s_os.mqtt_handle,
                    DEBI_TOPIC_STATUS, "{\"pong\":true}", 0, 0, 0);
            }
        } else {
            ESP_LOGW(TAG, "unknown cmd: %s", c);
        }
    }

    cJSON_Delete(root);
}

/* ============================================================
 *  WiFi event handlers
 * ============================================================ */

static void on_wifi_connected(void *handler_arg, esp_event_base_t base,
                               int32_t id, void *event_data)
{
    ESP_LOGI(TAG, "WiFi/SenseCraft MQTT connected event");
    /* Our MQTT auto-reconnect handles the hub connection */
}

static void on_wifi_disconnected(void *handler_arg, esp_event_base_t base,
                                  int32_t id, void *event_data)
{
    ESP_LOGW(TAG, "WiFi/SenseCraft MQTT disconnected event");
}

/* ============================================================
 *  Detection relay
 * ============================================================ */

static void on_detection_event(void *handler_arg, esp_event_base_t base,
                                int32_t id, void *event_data)
{
    if (!event_data) return;
    uint32_t task_id = *(uint32_t *)event_data;

    const char *type = "unknown";
    switch (task_id) {
        case 0: type = "gesture"; break;
        case 1: type = "pet";     break;
        case 2: type = "person";  break;
    }

    debi_os_report_detection(type, 100);
}

/* ============================================================
 *  Periodic callbacks
 * ============================================================ */

static void heartbeat_cb(void *arg)
{
    if (!s_os.hub_connected || !s_os.mqtt_handle) return;

    s_os.heartbeat_seq++;

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddNumberToObject(root, "seq", s_os.heartbeat_seq);
    cJSON_AddStringToObject(root, "mode", MODE_NAMES[s_os.mode]);
    cJSON_AddStringToObject(root, "face",
        ui_face_state_name(ui_face_get_state()));
    cJSON_AddNumberToObject(root, "uptime",
        difftime(time(NULL), s_os.boot_time));
    cJSON_AddNumberToObject(root, "detections", s_os.detection_count);
    cJSON_AddNumberToObject(root, "heap",
        (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "ts", (double)time(NULL));

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        esp_mqtt_client_publish(s_os.mqtt_handle,
                                 DEBI_TOPIC_HEARTBEAT, json, 0, 0, 0);
        free(json);
    }
    cJSON_Delete(root);
}

static void sensor_report_cb(void *arg)
{
    debi_os_report_sensors();
}

/* ============================================================
 *  Status publish (retained)
 * ============================================================ */

static void publish_status(void)
{
    if (!s_os.hub_connected || !s_os.mqtt_handle) return;

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddStringToObject(root, "status", "online");
    cJSON_AddStringToObject(root, "mode", MODE_NAMES[s_os.mode]);
    cJSON_AddStringToObject(root, "client_id", DEBI_HUB_MQTT_CLIENT_ID);
    cJSON_AddStringToObject(root, "face",
        ui_face_state_name(ui_face_get_state()));
    cJSON_AddNumberToObject(root, "uptime",
        difftime(time(NULL), s_os.boot_time));
    cJSON_AddNumberToObject(root, "ts", (double)time(NULL));

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        /* Retained so hub sees it on reconnect */
        esp_mqtt_client_publish(s_os.mqtt_handle,
                                 DEBI_TOPIC_STATUS, json, 0, 1, 1);
        free(json);
    }
    cJSON_Delete(root);
}
