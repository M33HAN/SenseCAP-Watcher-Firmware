/**
 * @file debi_os.h
 * @brief Debi OS — Core State Machine & Hub Connection
 *
 * Central module that manages the Debi Guardian device:
 *   - Operating mode state machine (BOOT->CONNECTING->ACTIVE->NIGHT->ALERT)
 *   - MQTT connection to the Debi Guardian Pi hub
 *   - Publishes sensor data, detections, and face state to the hub
 *   - Receives commands from the hub (mode changes, alerts, config)
 *   - Heartbeat/watchdog for connection health
 *
 * Copyright (c) 2026 Debi Guardian
 */

#ifndef DEBI_OS_H
#define DEBI_OS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Operating modes ── */
typedef enum {
    DEBI_MODE_BOOT = 0,     /* Starting up, initialising hardware       */
    DEBI_MODE_CONNECTING,   /* WiFi up, connecting to hub MQTT          */
    DEBI_MODE_ACTIVE,       /* Normal operation, monitoring             */
    DEBI_MODE_NIGHT,        /* Night mode, reduced sensitivity          */
    DEBI_MODE_ALERT,        /* Alert triggered, urgent state            */
    DEBI_MODE_SETUP,        /* First-time setup / configuration         */
    DEBI_MODE_ERROR,        /* Error state, needs attention             */
    DEBI_MODE_COUNT,
} debi_mode_t;

/* ── Hub connection config ── */
#ifndef DEBI_HUB_MQTT_URI
#define DEBI_HUB_MQTT_URI       "mqtt://192.168.0.182:1883"
#endif

#ifndef DEBI_HUB_MQTT_CLIENT_ID
#define DEBI_HUB_MQTT_CLIENT_ID "debi-watcher-01"
#endif

/* MQTT topics */
#define DEBI_TOPIC_PREFIX       "debi/watcher"
#define DEBI_TOPIC_STATUS       DEBI_TOPIC_PREFIX "/status"
#define DEBI_TOPIC_DETECTION    DEBI_TOPIC_PREFIX "/detection"
#define DEBI_TOPIC_SENSOR       DEBI_TOPIC_PREFIX "/sensor"
#define DEBI_TOPIC_FACE         DEBI_TOPIC_PREFIX "/face"
#define DEBI_TOPIC_HEARTBEAT    DEBI_TOPIC_PREFIX "/heartbeat"
#define DEBI_TOPIC_CMD          DEBI_TOPIC_PREFIX "/cmd"
#define DEBI_TOPIC_CONFIG       DEBI_TOPIC_PREFIX "/config"

/* Timing */
#ifndef DEBI_HEARTBEAT_INTERVAL_S
#define DEBI_HEARTBEAT_INTERVAL_S   30
#endif

#ifndef DEBI_SENSOR_REPORT_INTERVAL_S
#define DEBI_SENSOR_REPORT_INTERVAL_S 60
#endif

/* ── Public API ── */

/**
 * @brief Initialise Debi OS.
 *
 * Sets up the state machine, starts MQTT connection to hub,
 * registers event listeners for detections and sensor data.
 * Call after app_wifi_init() and debi_face_bridge_init().
 */
void debi_os_init(void);

/**
 * @brief Shut down Debi OS.
 */
void debi_os_deinit(void);

/**
 * @brief Get current operating mode.
 */
debi_mode_t debi_os_get_mode(void);

/**
 * @brief Request a mode change.
 *
 * Validates the transition and updates face state accordingly.
 */
void debi_os_set_mode(debi_mode_t mode);

/**
 * @brief Get mode name string for logging.
 */
const char *debi_os_mode_name(debi_mode_t mode);

/**
 * @brief Check if hub MQTT connection is active.
 */
bool debi_os_hub_connected(void);

/**
 * @brief Publish a detection event to the hub.
 *
 * @param type   Detection type string ("person", "pet", "gesture")
 * @param score  Confidence score (0-100)
 */
void debi_os_report_detection(const char *type, int score);

/**
 * @brief Publish current sensor readings to the hub.
 */
void debi_os_report_sensors(void);

/** Get the MQTT client handle for publishing */
/** Get the MQTT client handle for publishing (returns esp_mqtt_client_handle_t) */
void *debi_os_get_mqtt_handle(void);

#ifdef __cplusplus
}
#endif
#endif /* DEBI_OS_H */
