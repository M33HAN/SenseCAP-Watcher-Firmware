/**
 * @file debi_comms.h
 * @brief Debi Guardian — Communications layer
 *
 * Extends the basic MQTT in debi_os with:
 *   - Outbound message queue (survives brief disconnects)
 *   - Expanded hub command handling (mute, volume, play, reboot, OTA)
 *   - Command acknowledgement protocol (cmd_id echo)
 *   - Connection health metrics (latency, reconnect count)
 *   - Configuration sync from hub (timeouts, schedules, sensitivity)
 *
 * Architecture:
 *   Hub ←→ MQTT broker (Mosquitto on Pi) ←→ debi_os MQTT client
 *                                              ↕
 *                                         debi_comms
 *                                         (queue + cmd dispatch + ack)
 *
 * debi_comms does NOT own the MQTT client — debi_os does.
 * debi_comms hooks into debi_os by:
 *   1. debi_os calls debi_comms_handle_message() on incoming data
 *   2. Other modules call debi_comms_publish() instead of raw MQTT
 *   3. On reconnect, debi_os calls debi_comms_on_connected() to flush queue
 *
 * Copyright (c) 2026 Debi Guardian
 */

#ifndef DEBI_COMMS_H
#define DEBI_COMMS_H

#include <stdint.h>
#include <stdbool.h>
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* —— Queue config —— */
#ifndef DEBI_COMMS_QUEUE_SIZE
#define DEBI_COMMS_QUEUE_SIZE       16     /* Max queued outbound messages */
#endif

#ifndef DEBI_COMMS_MSG_MAX_LEN
#define DEBI_COMMS_MSG_MAX_LEN      512    /* Max payload bytes per message */
#endif

/* —— Health thresholds —— */
#ifndef DEBI_COMMS_PING_INTERVAL_S
#define DEBI_COMMS_PING_INTERVAL_S  60     /* Ping hub every 60s for latency */
#endif

#ifndef DEBI_COMMS_STALE_TIMEOUT_S
#define DEBI_COMMS_STALE_TIMEOUT_S  180    /* Hub considered stale after 3 min */
#endif

/* —— Connection health snapshot —— */
typedef struct {
    bool     connected;
    int      reconnect_count;
    int64_t  last_msg_time_us;     /* esp_timer_get_time of last rx */
    int      rtt_ms;               /* last measured round-trip (ping/pong) */
    int      queued_count;         /* messages waiting in outbound queue */
} debi_comms_health_t;

/* —— Configuration pushed from hub */ 
typedef struct {
    int  idle_timeout_s;           /* Override DEBI_BRIDGE_IDLE_TIMEOUT_S */
    int  concerned_timeout_s;      /* Override DEBI_BRIDGE_CONCERNED_TIMEOUT_S */
    int  volume;                   /* 0-100 */
    bool mute;
    bool night_auto;               /* Auto night mode by schedule */
    int  night_start_hour;         /* 0-23 */
    int  night_end_hour;           /* 0-23 */
} debi_comms_config_t;

/**
 * @brief Initialise the comms layer.
 *
 * Call after debi_os_init().  Does NOT create an MQTT client —
 * uses the one from debi_os.
 */
void debi_comms_init(void);

/**
 * @brief Shut down the comms layer.
 */
void debi_comms_deinit(void);

/**
 * @brief Called by debi_os when the MQTT client connects.
 *
 * Flushes the outbound queue and resets health counters.
 *
 * @param client  The esp_mqtt_client handle from debi_os
 */
void debi_comms_on_connected(esp_mqtt_client_handle_t client);

/**
 * @brief Called by debi_os when the MQTT client disconnects.
 */
void debi_comms_on_disconnected(void);

/**
 * @brief Route an incoming MQTT message through the comms layer.
 *
 * Called by debi_os mqtt_event_handler on MQTT_EVENT_DATA.
 * Handles command dispatch, ack generation, config sync, and pong.
 *
 * @param topic     Topic string (null-terminated)
 * @param data      Payload data
 * @param data_len  Payload length
 */
void debi_comms_handle_message(const char *topic,
                                const char *data, int data_len);

/**
 * @brief Publish a message to the hub, with queuing.
 *
 * If connected, publishes immediately.
 * If disconnected, queues the message for delivery on reconnect.
 *
 * @param topic    MQTT topic
 * @param payload  JSON payload string
 * @param qos      0, 1, or 2
 * @param retain   true for retained messages
 * @return         0 on success, -1 if queue full
 */
int debi_comms_publish(const char *topic, const char *payload,
                        int qos, bool retain);

/**
 * @brief Get current connection health snapshot.
 */
debi_comms_health_t debi_comms_get_health(void);

/**
 * @brief Get the current hub-pushed configuration.
 */
debi_comms_config_t debi_comms_get_config(void);

/**
 * @brief Check if the hub connection is considered healthy.
 *
 * Healthy = connected AND last message within STALE_TIMEOUT.
 */
bool debi_comms_hub_healthy(void);

#ifdef __cplusplus
}
#endif

#endif /* DEBI_COMMS_H */
