#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <string.h>
#include "freertos/event_groups.h"

static const char *TAG = "wifi_manager";
static EventGroupHandle_t s_wifi_event_group;
static char sta_ssid[32];
static char sta_password[64];
bool wifi_connected = false;

void wifi_manager_init(void) {
    // Dummy function, real implementation would initialize WiFi hardware
}

void wifi_manager_start_ap(void)
{
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "ESP32_CLOCK",
            .password = "12345678",
            .ssid_len = strlen("ESP32_CLOCK"),
            .channel = 1,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .max_connection = 4,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_LOGI(TAG, "AP SSID:%s password:%s",
             "ESP32_CLOCK",
             "12345678");
}

esp_err_t wifi_manager_connect_to_ap(void)
{
    s_wifi_event_group = xEventGroupCreate();

    wifi_config_t wifi_config;
    if (wifi_manager_load_sta_config()) {
        strncpy((char*)wifi_config.sta.ssid, sta_ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char*)wifi_config.sta.password, sta_password, sizeof(wifi_config.sta.password));
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;


        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "Connecting to AP SSID:%s", sta_ssid);
    } else {
        ESP_LOGE(TAG, "Failed to load STA config");
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool wifi_manager_load_sta_config(void) {
    // Dummy function, real implementation would load from NVS
    return false;
}