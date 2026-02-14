/**
 * @file debi_taskflow.c
 * @brief Debi Task Flow - Always-on person detection
 *
 * Simplified task flow that starts person detection on boot
 * and keeps it running. No cloud dependency, no complex state.
 *
 * Copyright (c) 2026 Debi Guardian
 */

#include "debi_taskflow.h"
#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_loops.h"
#include "data_defs.h"

static const char *TAG = "debi_tf";

esp_err_t debi_taskflow_init(void)
{
    ESP_LOGI(TAG, "Debi task flow init - starting person detection");

    /* Post the person detection task flow to start via local event.
     * Task ID 2 = person detection in factory local taskflow mapping. */
    uint32_t tf_num = 2;  /* 0=gesture, 1=pet, 2=person */
    esp_err_t ret = esp_event_post_to(app_event_loop_handle,
                                       VIEW_EVENT_BASE,
                                       VIEW_EVENT_TASK_FLOW_START_BY_LOCAL,
                                       &tf_num, sizeof(tf_num),
                                       pdMS_TO_TICKS(5000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start person detection: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Person detection task flow started");
    }
    return ret;
}

void debi_taskflow_deinit(void)
{
    ESP_LOGI(TAG, "Debi task flow deinit");
    esp_event_post_to(app_event_loop_handle,
                      VIEW_EVENT_BASE,
                      VIEW_EVENT_TASK_FLOW_STOP,
                      NULL, 0,
                      pdMS_TO_TICKS(1000));
}
