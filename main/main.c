#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "app_config.h"
#include "display_manager.h"
#include "wifi_manager.h"
#include "time_utils.h"
#include "web_server.h"

extern struct tm current_time;
void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    display_manager_init();
    display_message("INIT");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    display_clear();

    wifi_manager_init();

    if (wifi_manager_load_sta_config()) {
        if (wifi_manager_connect_to_ap() == ESP_OK) {
            // Connection successful, wifi_connected is set in the wifi_manager
        } else {
            // Connection failed, start AP mode
            display_message("FAIL");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            wifi_manager_start_ap();
            display_message("AP ON");
        }
    } else {
        // No STA config found, start AP mode
        wifi_manager_start_ap();
        display_message("AP ON");
    }

    web_server_start();

    if (wifi_connected) {
        sync_time();
    }

    while (1) {
        if (wifi_connected) {
            update_time();
            display_manager_show_time(current_time.tm_hour, current_time.tm_min, current_time.tm_sec);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}