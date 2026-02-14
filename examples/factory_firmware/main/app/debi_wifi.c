#include "debi_wifi.h"
#include "debi_os.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "data_defs.h"
#include "event_loops.h"
#include <string.h>

static const char *TAG = "debi-wifi";

#define DEBI_WIFI_SSID     "prettysly4awifi"
#define DEBI_WIFI_PASSWORD "Batman2021"

extern esp_event_loop_handle_t app_event_loop_handle;

/* Expose mqtt_connect from debi_os.c */
extern void debi_os_mqtt_start(void);

static void debi_wifi_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(8000));
    ESP_LOGI(TAG, "Posting WiFi connect event for SSID: %s", DEBI_WIFI_SSID);

    struct view_data_wifi_config cfg = { 0 };
    strlcpy(cfg.ssid, DEBI_WIFI_SSID, sizeof(cfg.ssid));
    strlcpy(cfg.password, DEBI_WIFI_PASSWORD, sizeof(cfg.password));
    cfg.have_password = true;

    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,
                      VIEW_EVENT_WIFI_CONNECT, &cfg,
                      sizeof(struct view_data_wifi_config),
                      pdMS_TO_TICKS(5000));

    /* Wait for WiFi to actually connect and get IP */
    vTaskDelay(pdMS_TO_TICKS(15000));

    /* Check if we got an IP */
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        ESP_LOGI(TAG, "WiFi connected! IP: " IPSTR, IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "Triggering MQTT connect directly...");
        debi_os_mqtt_start();
    } else {
        ESP_LOGW(TAG, "WiFi not connected after 15s, will retry...");
        /* Retry once more */
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            ESP_LOGI(TAG, "WiFi connected on retry! IP: " IPSTR, IP2STR(&ip_info.ip));
            debi_os_mqtt_start();
        } else {
            ESP_LOGE(TAG, "WiFi connection failed after retries");
        }
    }

    vTaskDelete(NULL);
}

esp_err_t debi_wifi_connect(void)
{
    ESP_LOGI(TAG, "Scheduling WiFi credential injection...");
    xTaskCreate(debi_wifi_task, "debi_wifi", 4096, NULL, 5, NULL);
    return ESP_OK;
}
