#include "debi_wifi.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

#define DEBI_WIFI_SSID     "prettysly4awifi"
#define DEBI_WIFI_PASSWORD "Batman2021"

static const char *TAG = "debi-wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
#define MAX_RETRY 15

extern void debi_os_mqtt_start(void);

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started, calling esp_wifi_connect()...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "STA connected to AP!");
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *evt = (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "Disconnected, reason=%d", evt->reason);
                if (s_retry_num < MAX_RETRY) {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGW(TAG, "Retry %d/%d...", s_retry_num, MAX_RETRY);
                } else {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    ESP_LOGE(TAG, "WiFi failed after %d retries", MAX_RETRY);
                }
                break;
            }
            default:
                ESP_LOGI(TAG, "WiFi event: %ld", (long)event_id);
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "*** Got IP: " IPSTR " ***", IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void debi_wifi_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(8000));
    ESP_LOGI(TAG, "=== Starting WiFi connection to '%s' ===", DEBI_WIFI_SSID);

    s_wifi_event_group = xEventGroupCreate();

    esp_event_handler_instance_t inst_any_id;
    esp_event_handler_instance_t inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &wifi_event_handler, NULL, &inst_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler, NULL, &inst_got_ip));

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, DEBI_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, DEBI_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_LOGI(TAG, "Setting WiFi mode STA...");
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "set_mode result: %s", esp_err_to_name(err));

    ESP_LOGI(TAG, "Setting WiFi config SSID='%s'...", DEBI_WIFI_SSID);
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    ESP_LOGI(TAG, "set_config result: %s", esp_err_to_name(err));

    ESP_LOGI(TAG, "Starting WiFi...");
    err = esp_wifi_start();
    ESP_LOGI(TAG, "wifi_start result: %s", esp_err_to_name(err));

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_start failed, trying stop then start...");
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(1000));
        err = esp_wifi_start();
        ESP_LOGI(TAG, "wifi_start retry result: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Waiting for connection (60s timeout)...");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(60000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "=== WiFi CONNECTED! Starting MQTT... ===");
        vTaskDelay(pdMS_TO_TICKS(2000));
        debi_os_mqtt_start();
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "=== WiFi FAILED after retries ===");
    } else {
        ESP_LOGE(TAG, "=== WiFi TIMED OUT ===");
    }

    vTaskDelete(NULL);
}

esp_err_t debi_wifi_connect(void)
{
    ESP_LOGI(TAG, "Scheduling WiFi connection (8s delay)...");
    xTaskCreate(debi_wifi_task, "debi_wifi", 4096, NULL, 5, NULL);
    return ESP_OK;
}
