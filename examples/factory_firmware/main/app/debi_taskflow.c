/**
 * @file debi_taskflow.c
 * @brief Debi Task Flow - Always-on person detection
 *
 * Starts person detection after the task flow engine is ready.
 * Uses a delayed FreeRTOS task to ensure event handlers are registered.
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

static void debi_taskflow_start_task(void *arg)
{
    /* Wait for task flow engine to fully initialise */
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "Starting person detection task flow");

    uint32_t tf_num = 2;  /* 0=gesture, 1=pet, 2=person */
    esp_err_t ret = esp_event_post_to(app_event_loop_handle,
                                       VIEW_EVENT_BASE,
                                       VIEW_EVENT_TASK_FLOW_START_BY_LOCAL,
                                       &tf_num, sizeof(tf_num),
                                       pdMS_TO_TICKS(5000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start person detection: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Person detection started successfully");
    }

    vTaskDelete(NULL);
}

esp_err_t debi_taskflow_init(void)
{
    ESP_LOGI(TAG, "Debi task flow init - will start person detection in 3s");

    /* Launch in a separate task so we don't block the init sequence */
    xTaskCreate(debi_taskflow_start_task, "debi_tf", 4096, NULL, 5, NULL);

    return ESP_OK;
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
