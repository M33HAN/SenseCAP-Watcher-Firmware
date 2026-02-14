/**
 * @file debi_camera.c
 * @brief Debi Camera - Frame streaming to hub via MQTT
 *
 * Publishes JPEG frames from WiseEye2 to debi/watcher/camera/frame
 * at a controlled rate (max 2 FPS) for hub-side AI processing.
 * Image data from WiseEye2 is base64-encoded JPEG.
 *
 * Copyright (c) 2026 Debi Guardian
 */

#include "debi_camera.h"
#include "debi_os.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"

static const char *TAG = "debi_cam";

/* Rate limiting: max 2 frames per second (500ms interval) */
#define DEBI_CAMERA_MIN_INTERVAL_MS  500
static int64_t s_last_frame_time_us = 0;
static uint32_t s_frame_count = 0;

void debi_camera_forward_frame(const struct tf_module_ai_camera_preview_info *preview)
{
    if (!preview || !preview->img.p_buf || preview->img.len == 0) {
        return;
    }

    /* Rate limit */
    int64_t now = esp_timer_get_time();
    if ((now - s_last_frame_time_us) < (DEBI_CAMERA_MIN_INTERVAL_MS * 1000)) {
        return;
    }
    s_last_frame_time_us = now;

    /* Get MQTT handle */
    esp_mqtt_client_handle_t mqtt = debi_os_get_mqtt_handle();
    if (!mqtt) {
        return;  /* MQTT not connected yet */
    }

    /* Publish frame - the image data is base64-encoded JPEG from WiseEye2.
     * We send it as-is; the hub will decode it. */
    int msg_id = esp_mqtt_client_publish(mqtt,
                                          "debi/watcher/camera/frame",
                                          (const char *)preview->img.p_buf,
                                          preview->img.len,
                                          0,   /* QoS 0 for speed */
                                          0);  /* no retain */

    s_frame_count++;
    if (s_frame_count % 10 == 1) {
        ESP_LOGI(TAG, "Frame #%lu sent (%lu bytes)", s_frame_count, preview->img.len);
    }
}
