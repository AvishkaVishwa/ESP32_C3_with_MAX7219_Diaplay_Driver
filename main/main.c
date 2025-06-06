#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "CLOCK"

// MAX7219 SPI configuration
#define PIN_NUM_MOSI 2
#define PIN_NUM_CLK  4
#define PIN_NUM_CS   5

// Buzzer pin and Dismiss button pin
#define BUZZER_PIN   6
#define DISMISS_BUTTON_PIN 7

spi_device_handle_t spi;

// Segment codes for digits 0-9 (common cathode)
const uint8_t digit_to_segment[] = {
    0x7E, // 0
    0x30, // 1
    0x6D, // 2
    0x79, // 3
    0x33, // 4
    0x5B, // 5
    0x5F, // 6
    0x70, // 7
    0x7F, // 8
    0x7B  // 9
};

static int alarm_hour = -1;
static int alarm_minute = -1;
static bool alarm_triggered = false;

void max7219_send(uint8_t address, uint8_t data) {
    uint8_t tx_data[2] = {address, data};
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx_data
    };
    spi_device_transmit(spi, &t);
}

void max7219_init() {
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10000000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 7
    };
    spi_bus_add_device(SPI2_HOST, &devcfg, &spi);

    max7219_send(0x0F, 0x00);
    max7219_send(0x0C, 0x01);
    max7219_send(0x0B, 0x05);
    max7219_send(0x0A, 0x0F);
    max7219_send(0x09, 0x00);

    for (int i = 1; i <= 6; i++) {
        max7219_send(i, 0x00);
    }
}

void display_time(int hour, int minute, int second) {
    int digits[6] = {
        hour / 10,
        hour % 10,
        minute / 10,
        minute % 10,
        second / 10,
        second % 10
    };
    for (int i = 0; i < 6; i++) {
        max7219_send(i + 1, digit_to_segment[digits[i]]);
    }
}

esp_err_t get_handler(httpd_req_t *req) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char html[2048];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<title>Smart Clock</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; background-color: #f4f4f4; color: #333; padding: 20px; }"
        "h1 { color: #007ACC; }"
        "form { background-color: #fff; padding: 15px; border-radius: 5px; margin-bottom: 20px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }"
        "input[type=number] { width: 60px; padding: 5px; margin-right: 10px; border: 1px solid #ccc; border-radius: 3px; }"
        "input[type=submit] { background-color: #007ACC; color: white; border: none; padding: 8px 15px; border-radius: 3px; cursor: pointer; }"
        "input[type=submit]:hover { background-color: #005EA6; }"
        "</style>"
        "</head>"
        "<body>"
        "<h1>ESP32-C3 Clock</h1>"
        "<h2>Current Time: %02d:%02d:%02d</h2>"

        "<h2>Set Time</h2>"
        "<form action='/settime' method='post'>"
        "Hour: <input type='number' name='hour' min='0' max='23'>"
        "Minute: <input type='number' name='minute' min='0' max='59'>"
        "Second: <input type='number' name='second' min='0' max='59'>"
        "<input type='submit' value='Set Time'>"
        "</form>"

        "<h2>Set Alarm</h2>"
        "<form action='/setalarm' method='post'>"
        "Hour: <input type='number' name='alarm_hour' min='0' max='23'>"
        "Minute: <input type='number' name='alarm_minute' min='0' max='59'>"
        "<input type='submit' value='Set Alarm'>"
        "</form>"

        "<h2>Dismiss Alarm</h2>"
        "<form action='/dismiss' method='post'>"
        "<input type='submit' value='Dismiss Alarm'>"
        "</form>"

        "</body></html>",
        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t settime_post_handler(httpd_req_t *req) {
    char buf[100];
    int ret, remaining = req->content_len;
    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            return ESP_FAIL;
        }
        remaining -= ret;
    }
    buf[req->content_len] = '\0';

    int hour = 0, minute = 0, second = 0;
    sscanf(buf, "hour=%d&minute=%d&second=%d", &hour, &minute, &second);

    struct tm timeinfo = {
        .tm_year = 123, // since 1900
        .tm_mon = 0,
        .tm_mday = 1,
        .tm_hour = hour,
        .tm_min = minute,
        .tm_sec = second
    };
    time_t now = mktime(&timeinfo);
    struct timeval tv = {.tv_sec = now};
    settimeofday(&tv, NULL);

    ESP_LOGI(TAG, "Time set to %02d:%02d:%02d", hour, minute, second);
    httpd_resp_sendstr(req, "Time updated");
    return ESP_OK;
}

esp_err_t setalarm_post_handler(httpd_req_t *req) {
    char buf[100];
    int ret, remaining = req->content_len;
    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            return ESP_FAIL;
        }
        remaining -= ret;
    }
    buf[req->content_len] = '\0';

    int hour = 0, minute = 0;
    sscanf(buf, "alarm_hour=%d&alarm_minute=%d", &hour, &minute);

    alarm_hour = hour;
    alarm_minute = minute;
    alarm_triggered = false;

    ESP_LOGI(TAG, "Alarm set to %02d:%02d", alarm_hour, alarm_minute);
    httpd_resp_sendstr(req, "Alarm time updated");
    return ESP_OK;
}

esp_err_t dismiss_post_handler(httpd_req_t *req) {
    gpio_set_level(BUZZER_PIN, 0);
    alarm_triggered = false;
    ESP_LOGI(TAG, "Alarm dismissed by web interface.");
    httpd_resp_sendstr(req, "Alarm dismissed.");
    return ESP_OK;
}

httpd_handle_t start_webserver() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    httpd_start(&server, &config);

    httpd_uri_t get_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_handler
    };
    httpd_register_uri_handler(server, &get_uri);

    httpd_uri_t settime_post_uri = {
        .uri = "/settime",
        .method = HTTP_POST,
        .handler = settime_post_handler
    };
    httpd_register_uri_handler(server, &settime_post_uri);

    httpd_uri_t setalarm_post_uri = {
        .uri = "/setalarm",
        .method = HTTP_POST,
        .handler = setalarm_post_handler
    };
    httpd_register_uri_handler(server, &setalarm_post_uri);

    httpd_uri_t dismiss_post_uri = {
        .uri = "/dismiss",
        .method = HTTP_POST,
        .handler = dismiss_post_handler
    };
    httpd_register_uri_handler(server, &dismiss_post_uri);

    return server;
}

void wifi_init_softap() {
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "Clock",
            .ssid_len = strlen("Clock"),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        }
    };
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();
}

void app_main() {
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_softap();
    max7219_init();
    start_webserver();

    gpio_config_t buzzer_conf = {
        .pin_bit_mask = (1ULL << BUZZER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&buzzer_conf);

    gpio_config_t dismiss_button_conf = {
        .pin_bit_mask = (1ULL << DISMISS_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&dismiss_button_conf);

    while (1) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        display_time(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        if (alarm_hour >= 0 && alarm_minute >= 0) {
            if (timeinfo.tm_hour == alarm_hour &&
                timeinfo.tm_min == alarm_minute &&
                !alarm_triggered) {
                ESP_LOGI(TAG, "ALARM TRIGGERED!");
                gpio_set_level(BUZZER_PIN, 1);
                alarm_triggered = true;
            } else if (timeinfo.tm_min != alarm_minute) {
                gpio_set_level(BUZZER_PIN, 0);
                alarm_triggered = false;
            }
        }

        // Check dismiss button
        if (gpio_get_level(DISMISS_BUTTON_PIN) == 0) {
            gpio_set_level(BUZZER_PIN, 0);
            alarm_triggered = false;
            ESP_LOGI(TAG, "Alarm dismissed by button.");
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
