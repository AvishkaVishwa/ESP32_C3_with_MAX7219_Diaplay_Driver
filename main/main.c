#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
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
#include "esp_sntp.h"  // Use ESP-IDF's SNTP implementation
#include "esp_timer.h" // For periodic timer

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define TAG "CLOCK"

/*
 * ESP32 WROOM-32D Pin Assignments for Clock Project:
 * 
 * MAX7219 7-Segment Display:
 * - VCC  -> 3.3V or 5V
 * - GND  -> GND
 * - DIN  -> GPIO23 (MOSI)
 * - CS   -> GPIO5
 * - CLK  -> GPIO18 (SCK)
 * 
 * Other Components:
 * - Buzzer        -> GPIO4
 * - Dismiss Button-> GPIO0 (BOOT button - built-in)
 * - Seconds LED   -> GPIO2 (Built-in LED)
 * - AM/PM LED     -> GPIO19
 * 
 * Notes:
 * - GPIO0: Boot button (pulled up externally, LOW when pressed)
 * - GPIO2: Built-in LED (some boards)
 * - GPIO18/23: Standard SPI pins
 * - GPIO4: Safe general purpose pin
 * - GPIO5: Safe general purpose pin
 * - GPIO19: Safe general purpose pin
 */

// MAX7219 SPI configuration for ESP32 WROOM-32D
#define PIN_NUM_MOSI 23  // GPIO23 for MOSI (SPI data) - Standard SPI MOSI
#define PIN_NUM_CLK  18  // GPIO18 for SCK (SPI clock) - Standard SPI CLK
#define PIN_NUM_CS   5   // GPIO5 for CS (chip select)

// Other pin configurations for ESP32 WROOM-32D
#define BUZZER_PIN   4   // GPIO4 - Buzzer output
#define DISMISS_BUTTON_PIN 0  // GPIO0 (BOOT button) - Button input
#define SECONDS_LED_PIN 2    // GPIO2 (Built-in LED) - LED indicator for seconds
#define AMPM_LED_PIN   19    // GPIO19 - LED for AM/PM indication

// NTP sync interval in milliseconds (1 hour)
#define NTP_SYNC_INTERVAL_MS 3600000

// WiFi reconnection delay
#define WIFI_RECONNECT_DELAY_MS 10000

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

// Time tracking variables for interval notifications
static int last_hour = -1;    // Track the previous hour
static int last_minute = -1;  // Track the previous minute
static int last_second = -1;  // Track previous second for LED blinking

// Button debouncing variables - ESP32 WROOM-32D specific
static bool last_button_state = true;  // GPIO0 is pulled up, so true = not pressed
static uint32_t last_button_change = 0;
static const uint32_t DEBOUNCE_DELAY_MS = 100;  // Longer debounce for GPIO0

// Sri Lanka timezone (IST - UTC+5:30)
static int timezone_hours = 5;    // Hours offset from UTC
static int timezone_minutes = 30;  // Minutes offset

// WiFi settings - FIX: Default to empty values to prevent self-connection
static char wifi_ssid[32] = "";  // Empty by default - will be set via web interface
static char wifi_password[64] = "";  // Empty by default
static bool wifi_has_password = false;  // Default to false since we have no default password
static bool wifi_sta_connected = false;
static int ap_client_count = 0;
static bool reconnect_timer_active = false;
static esp_timer_handle_t reconnect_timer = NULL;

// Forward declarations
void max7219_send(uint8_t address, uint8_t data);
void max7219_init(void);
void test_display(void);
void single_beep(void);
void double_beep(void);
void display_time(int hour, int minute, int second);
void time_sync_notification_cb(struct timeval *tv);
void sync_time_with_ntp(void* pvParameters);
void ntp_timer_callback(void* arg);
void start_periodic_ntp_sync(void);
void wifi_reconnect_timer_callback(void* arg);
void start_wifi_reconnect_timer(void);
void reconnect_to_home_wifi(void);
void save_wifi_settings(void);
void load_wifi_settings(void);
httpd_handle_t start_webserver(void);
void wifi_init_softap(void);
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
char hex_to_char(char first, char second);
void url_decode(char *src, char *dest, size_t dest_size);

// URL decoding helper functions for handling special characters in form data
char hex_to_char(char first, char second) {
    char hex[3] = {first, second, '\0'};
    return (char)strtol(hex, NULL, 16);
}

void url_decode(char *src, char *dest, size_t dest_size) {
    size_t i = 0, j = 0;
    
    while (src[i] && j < dest_size - 1) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            dest[j++] = hex_to_char(src[i+1], src[i+2]);
            i += 3;
        } else if (src[i] == '+') {
            dest[j++] = ' ';
            i++;
        } else {
            dest[j++] = src[i++];
        }
    }
    
    dest[j] = '\0';
}

// SPI send function for MAX7219
void max7219_send(uint8_t address, uint8_t data) {
    uint8_t tx_data[2] = {address, data};
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx_data
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI send failed: %s", esp_err_to_name(ret));
    }
}

// Initialize MAX7219 display
void max7219_init(void) {
    ESP_LOGI(TAG, "Initializing SPI bus with MOSI:%d, CLK:%d, CS:%d", 
              PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS);
              
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .miso_io_num = -1,  // We don't use MISO for the MAX7219
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,  // Explicit size
    };
    
    // Use HSPI_HOST for ESP32 WROOM-32D (SPI2)
    esp_err_t ret = spi_bus_initialize(HSPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 5000000,   // 5 MHz - reduced for better stability
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 7,
        .flags = 0,
    };
    
    ret = spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return;
    }

    // Add small delay before initializing MAX7219
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // Initialize MAX7219 with proper sequence
    max7219_send(0x0C, 0x00);  // Shutdown register - shutdown mode first
    max7219_send(0x0F, 0x00);  // Display test register - normal operation
    max7219_send(0x09, 0x00);  // Decode mode register - no decode
    max7219_send(0x0B, 0x05);  // Scan limit register - display digits 0-5
    max7219_send(0x0A, 0x08);  // Intensity register - medium brightness
    
    // Clear all digits
    for (int i = 1; i <= 6; i++) {
        max7219_send(i, 0x00);
    }
    
    // Enable normal operation
    max7219_send(0x0C, 0x01);  // Shutdown register - normal operation
    
    ESP_LOGI(TAG, "MAX7219 initialized successfully");
}

// Single beep function - implementation added to fix linker error
void single_beep(void) {
    gpio_set_level(BUZZER_PIN, 1);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    gpio_set_level(BUZZER_PIN, 0);
}

// Double beep function
void double_beep(void) {
    // First beep
    gpio_set_level(BUZZER_PIN, 1);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    gpio_set_level(BUZZER_PIN, 0);
    
    // Pause between beeps
    vTaskDelay(200 / portTICK_PERIOD_MS);
    
    // Second beep
    gpio_set_level(BUZZER_PIN, 1);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    gpio_set_level(BUZZER_PIN, 0);
}

// Display time in 12-hour format
void display_time(int hour, int minute, int second) {
    // Convert to 12-hour format
    bool is_pm = hour >= 12;
    int display_hour = hour % 12;
    if (display_hour == 0) display_hour = 12; // 0 hour in 12-hour format is 12 AM/PM
    
    int digits[6] = {
        display_hour / 10,
        display_hour % 10,
        minute / 10,
        minute % 10,
        second / 10,
        second % 10
    };
    
    // Display time on 7-segment display
    for (int i = 0; i < 6; i++) {
        max7219_send(i + 1, digit_to_segment[digits[i]]);
    }
    
    // Use LED to indicate PM (ON for PM, OFF for AM)
    gpio_set_level(AMPM_LED_PIN, is_pm ? 1 : 0);
}

// Time sync notification callback
void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

// Function to synchronize time with NTP servers
void sync_time_with_ntp(void* pvParameters) {
    ESP_LOGI(TAG, "Initializing SNTP");
    
    // Set timezone for Sri Lanka (IST)
    char tz_str[32];
    // POSIX timezone format: For UTC+5:30, we need to use negative offset in POSIX format
    // POSIX uses the opposite sign: UTC+5:30 becomes IST-5:30
    if (timezone_minutes == 0) {
        snprintf(tz_str, sizeof(tz_str), "IST-%d", timezone_hours);
    } else {
        snprintf(tz_str, sizeof(tz_str), "IST-%d:%02d", timezone_hours, timezone_minutes);
    }
    setenv("TZ", tz_str, 1);
    tzset();
    
    // Initialize SNTP client
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    
    // Register time sync notification
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    
    // Initialize SNTP
    esp_sntp_init();
    
    // Wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 15;
    
    while (timeinfo.tm_year < (2020 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    
    if (timeinfo.tm_year < (2020 - 1900)) {
        ESP_LOGE(TAG, "Failed to get time from NTP server");
    } else {
        ESP_LOGI(TAG, "Time synchronized: %04d-%02d-%02d %02d:%02d:%02d",
                timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
    
    vTaskDelete(NULL);
}

// Timer callback for periodic NTP sync
void ntp_timer_callback(void* arg) {
    if (wifi_sta_connected) {
        xTaskCreate(
            sync_time_with_ntp,
            "ntp_sync_task",
            4096,
            NULL,
            5,
            NULL);
    }
}

// Start periodic NTP synchronization
void start_periodic_ntp_sync(void) {
    // First immediate sync
    xTaskCreate(
        sync_time_with_ntp,
        "ntp_sync_task",
        4096,
        NULL,
        5,
        NULL);
        
    // Schedule periodic sync
    const esp_timer_create_args_t timer_args = {
        .callback = &ntp_timer_callback,
        .name = "ntp_sync"
    };
    
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, (uint64_t)NTP_SYNC_INTERVAL_MS * 1000));
    
    ESP_LOGI(TAG, "Periodic NTP sync scheduled every %d minutes", NTP_SYNC_INTERVAL_MS / 60000);
}

// Timer callback to reconnect to WiFi
void wifi_reconnect_timer_callback(void* arg) {
    reconnect_timer_active = false;
    if (ap_client_count == 0 && strlen(wifi_ssid) > 0 && wifi_has_password) {
        reconnect_to_home_wifi();
    }
}

// Start timer to reconnect to home WiFi after delay
void start_wifi_reconnect_timer(void) {
    if (reconnect_timer_active) {
        // Timer already running, cancel it
        esp_timer_stop(reconnect_timer);
    } else {
        // Create timer if it doesn't exist
        const esp_timer_create_args_t timer_args = {
            .callback = &wifi_reconnect_timer_callback,
            .name = "wifi_reconnect"
        };
        
        if (reconnect_timer == NULL) {
            ESP_ERROR_CHECK(esp_timer_create(&timer_args, &reconnect_timer));
        }
    }
    
    // Start the timer
    ESP_ERROR_CHECK(esp_timer_start_once(reconnect_timer, (uint64_t)WIFI_RECONNECT_DELAY_MS * 1000));
    reconnect_timer_active = true;
    ESP_LOGI(TAG, "WiFi reconnection scheduled in %d seconds", WIFI_RECONNECT_DELAY_MS / 1000);
}

// Reconnect to home WiFi network
void reconnect_to_home_wifi(void) {
    if (strlen(wifi_ssid) > 0 && wifi_has_password) {
        ESP_LOGI(TAG, "Reconnecting to home WiFi: %s", wifi_ssid);
        
        // Configure STA mode
        wifi_config_t sta_config = {0};
        strlcpy((char*)sta_config.sta.ssid, wifi_ssid, sizeof(sta_config.sta.ssid));
        strlcpy((char*)sta_config.sta.password, wifi_password, sizeof(sta_config.sta.password));
        
        // Set mode to APSTA
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        esp_wifi_connect();
    }
}

// Functions to save and load WiFi settings from NVS
void save_wifi_settings(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }
    
    err = nvs_set_str(my_handle, "wifi_ssid", wifi_ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving SSID: %s", esp_err_to_name(err));
    }
    
    err = nvs_set_str(my_handle, "wifi_pass", wifi_password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving password: %s", esp_err_to_name(err));
    }
    
    err = nvs_set_u8(my_handle, "wifi_has_pass", wifi_has_password ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving password flag: %s", esp_err_to_name(err));
    }
    
    // Save alarm settings too
    err = nvs_set_i32(my_handle, "alarm_hour", alarm_hour);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving alarm hour: %s", esp_err_to_name(err));
    }
    
    err = nvs_set_i32(my_handle, "alarm_minute", alarm_minute);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving alarm minute: %s", esp_err_to_name(err));
    }
    
    // Save timezone settings
    err = nvs_set_i32(my_handle, "tz_hours", timezone_hours);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving timezone hours: %s", esp_err_to_name(err));
    }
    
    err = nvs_set_i32(my_handle, "tz_minutes", timezone_minutes);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving timezone minutes: %s", esp_err_to_name(err));
    }
    
    nvs_commit(my_handle);
    nvs_close(my_handle);
    ESP_LOGI(TAG, "WiFi, alarm and timezone settings saved to NVS");
}

void load_wifi_settings(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS open failed, using default settings");
        return;
    }
    
    size_t required_size = sizeof(wifi_ssid);
    err = nvs_get_str(my_handle, "wifi_ssid", wifi_ssid, &required_size);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved SSID found, using default");
        wifi_ssid[0] = '\0';  // Ensure it's empty if not found
    }
    
    required_size = sizeof(wifi_password);
    err = nvs_get_str(my_handle, "wifi_pass", wifi_password, &required_size);
    if (err != ESP_OK) {
        wifi_password[0] = '\0';
    }
    
    uint8_t has_pass = 0;
    err = nvs_get_u8(my_handle, "wifi_has_pass", &has_pass);
    if (err != ESP_OK) {
        wifi_has_password = false;
    } else {
        wifi_has_password = (has_pass == 1);
    }
    
    // Load alarm settings
    int32_t saved_hour = -1;
    err = nvs_get_i32(my_handle, "alarm_hour", &saved_hour);
    if (err == ESP_OK) {
        alarm_hour = saved_hour;
    }
    
    int32_t saved_minute = -1;
    err = nvs_get_i32(my_handle, "alarm_minute", &saved_minute);
    if (err == ESP_OK) {
        alarm_minute = saved_minute;
    }
    
    // Load timezone settings
    int32_t saved_tz_hours = 5; // Default to Sri Lanka timezone (5 hours)
    err = nvs_get_i32(my_handle, "tz_hours", &saved_tz_hours);
    if (err == ESP_OK) {
        timezone_hours = saved_tz_hours;
    }
    
    int32_t saved_tz_minutes = 30; // Default to Sri Lanka timezone (30 minutes)
    err = nvs_get_i32(my_handle, "tz_minutes", &saved_tz_minutes);
    if (err == ESP_OK) {
        timezone_minutes = saved_tz_minutes;
    }
    
    nvs_close(my_handle);
    
    // Modified logging to avoid truncation warnings
    if (strlen(wifi_ssid) > 0) {
        ESP_LOGI(TAG, "Loaded WiFi SSID: %s", wifi_ssid);
    } else {
        ESP_LOGI(TAG, "No WiFi SSID loaded");
    }
    
    if (alarm_hour >= 0 && alarm_minute >= 0) {
        ESP_LOGI(TAG, "Loaded alarm time: %02d:%02d", alarm_hour, alarm_minute);
    }
    
    // Pre-format the timezone string for display
    char tz_str[64]; // Increased buffer size to prevent truncation
    snprintf(tz_str, sizeof(tz_str), "UTC+%d:%02d", timezone_hours, timezone_minutes);
    ESP_LOGI(TAG, "Loaded timezone: %s", tz_str);
}

esp_err_t get_handler(httpd_req_t *req) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Format the full date and time in local time format
    char datetime_str[64];
    snprintf(datetime_str, sizeof(datetime_str), 
             "%04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    // Create alarm status string
    char alarm_status[100];
    if (alarm_hour >= 0 && alarm_minute >= 0) {
        snprintf(alarm_status, sizeof(alarm_status), 
                "<h2>Current Alarm: %02d:%02d</h2>", 
                alarm_hour, alarm_minute);
    } else {
        snprintf(alarm_status, sizeof(alarm_status), 
                "<h2>No Alarm Set</h2>");
    }

    // Create current time string
    char current_time[20];
    snprintf(current_time, sizeof(current_time), 
             "%02d:%02d:%02d", 
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    // Set response content type
    httpd_resp_set_type(req, "text/html");
    
    // Send HTML in chunks to avoid large buffer
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<title>Smart Clock</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; background-color: #f4f4f4; color: #333; padding: 20px; }"
        "h1 { color: #007ACC; }"
        "form { background-color: #fff; padding: 15px; border-radius: 5px; margin-bottom: 20px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }"
        "input[type=number], input[type=text], input[type=password] { width: 120px; padding: 5px; margin-right: 10px; border: 1px solid #ccc; border-radius: 3px; }"
        "input[type=submit] { background-color: #007ACC; color: white; border: none; padding: 8px 15px; border-radius: 3px; cursor: pointer; }"
        "input[type=submit]:hover { background-color: #005EA6; }"
        ".warning { color: #ff0000; font-weight: bold; }"
        ".info { color: #007ACC; }"
        ".note { color: #666; font-style: italic; margin-top: 10px; }"
        ".datetime { font-size: 1.2em; color: #333; font-weight: bold; margin-bottom: 20px; }"
        "fieldset { border: 1px solid #ddd; padding: 10px; margin-bottom: 15px; }"
        "legend { font-weight: bold; color: #007ACC; }"
        "</style>"
        "</head>"
        "<body>"
        "<h1>ESP32 WROOM-32D Clock</h1>");

    // Send dynamic content in chunks
    char chunk[512];
    snprintf(chunk, sizeof(chunk), "<div class='datetime'>Current Date and Time: %s (IST/Sri Lanka Time)</div>", datetime_str);
    httpd_resp_sendstr_chunk(req, chunk);

    snprintf(chunk, sizeof(chunk), "<h2>Current Time: %s</h2>", current_time);
    httpd_resp_sendstr_chunk(req, chunk);
    
    // Send alarm status
    httpd_resp_sendstr_chunk(req, alarm_status);

    // Send forms
    httpd_resp_sendstr_chunk(req, 
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

        "<h2>Set Countdown Timer</h2>"
        "<form action='/setcountdown' method='post'>"
        "Hours: <input type='number' name='hours' min='0' max='23'>"
        "Minutes: <input type='number' name='minutes' min='0' max='59'>"
        "<input type='submit' value='Start Countdown'>"
        "</form>"

        "<h2>Dismiss Alarm</h2>"
        "<form action='/dismiss' method='post'>"
        "<input type='submit' value='Dismiss Alarm'>"
        "</form>");
        
    // Add timezone form
    httpd_resp_sendstr_chunk(req,
        "<h2>Set Timezone</h2>"
        "<form action='/settz' method='post'>"
        "<p>Sri Lanka uses IST (India Standard Time): UTC+5:30</p>");

    char tz_chunk[256]; // Increased buffer size to prevent truncation
    snprintf(tz_chunk, sizeof(tz_chunk),
        "Hours: <input type='number' name='tz_hours' value='%d' min='-12' max='14'>\n"
        "Minutes: <select name='tz_minutes'>"
        "<option value='0' %s>00</option>"
        "<option value='30' %s>30</option>"
        "</select>\n",
        timezone_hours,
        timezone_minutes == 0 ? "selected" : "",
        timezone_minutes == 30 ? "selected" : "");
    httpd_resp_sendstr_chunk(req, tz_chunk);

    httpd_resp_sendstr_chunk(req,
        "<input type='submit' value='Set Timezone'>"
        "<p class='note'>Current setting: UTC");

    snprintf(tz_chunk, sizeof(tz_chunk), "+%d:%02d</p></form>", timezone_hours, timezone_minutes);
    httpd_resp_sendstr_chunk(req, tz_chunk);

    // WiFi form - Break into smaller chunks to avoid buffer overflow
    httpd_resp_sendstr_chunk(req, 
        "<h2>WiFi Settings</h2>"
        "<form action='/setwifi' method='post' accept-charset='UTF-8'>"
        "<p>The clock creates its own 'Clock' network for configuration, but can also connect to your home WiFi for internet time sync.</p>"
        "<fieldset>"
        "<legend>Home WiFi Connection</legend>");

    // Format just the input fields with dynamic values
    snprintf(chunk, sizeof(chunk), 
        "Network Name: <input type='text' name='ssid' value='%s' maxlength='31'><br><br>"
        "Password: <input type='password' name='password' placeholder='WiFi password' maxlength='63'><br>",
        wifi_ssid);
    httpd_resp_sendstr_chunk(req, chunk);

    // Format the status separately
    snprintf(chunk, sizeof(chunk), 
        "<p class='info'>Current status: %s</p>",
        wifi_sta_connected ? "Connected" : "Not connected");
    httpd_resp_sendstr_chunk(req, chunk);

    // Send the remaining static content
    httpd_resp_sendstr_chunk(req,
        "<p class='note'>Note: While you are connected to the Clock's WiFi, the connection to home WiFi is temporarily paused. "
        "When you disconnect from the Clock's WiFi, it will automatically reconnect to your home network.</p>"
        "</fieldset>"
        "<p class='warning'>Note: Changing WiFi settings will restart the device.</p>"
        "<input type='submit' value='Update WiFi'>"
        "</form>");

    // Send the final chunk
    httpd_resp_sendstr_chunk(req, 
        "<h2>Sync Time with NTP</h2>"
        "<form action='/syncntp' method='post'>"
        "<input type='submit' value='Sync Time Now'>"
        "<p class='note'>Requires an active internet connection via home WiFi.</p>"
        "</form>"
        
        "<p>Created by: AvishkaVishwa</p>"
        "</body></html>");

    // Send empty chunk to signal end of response
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// Handler for /meta.json to prevent 404 errors
esp_err_t meta_json_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{}");
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

    // Get current date and time, then update only the time portion
    time_t current_time;
    struct tm timeinfo;
    time(&current_time);
    localtime_r(&current_time, &timeinfo);
    
    // Update only the time fields, keep the current date
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    
    time_t new_time = mktime(&timeinfo);
    struct timeval tv = {.tv_sec = new_time};
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

    // Save to NVS
    save_wifi_settings();

    ESP_LOGI(TAG, "Alarm set to %02d:%02d", alarm_hour, alarm_minute);
    httpd_resp_sendstr(req, "Alarm time updated");
    return ESP_OK;
}

esp_err_t setcountdown_post_handler(httpd_req_t *req) {
    char buf[100];
    int ret, remaining = req->content_len;
    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            return ESP_FAIL;
        }
        remaining -= ret;
    }
    buf[req->content_len] = '\0';

    int hours = 0, minutes = 0;
    sscanf(buf, "hours=%d&minutes=%d", &hours, &minutes);

    // Get current time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Calculate alarm time by adding the countdown duration
    timeinfo.tm_hour += hours;
    timeinfo.tm_min += minutes;
    
    // Normalize the time (handle overflow)
    mktime(&timeinfo);
    
    // Set the alarm
    alarm_hour = timeinfo.tm_hour;
    alarm_minute = timeinfo.tm_min;
    alarm_triggered = false;

    // Save to NVS
    save_wifi_settings();

    char response[100];
    snprintf(response, sizeof(response), 
             "Countdown timer set! Alarm will ring at %02d:%02d", 
             alarm_hour, alarm_minute);
    
    ESP_LOGI(TAG, "Countdown timer set for %d hours and %d minutes from now. Alarm at %02d:%02d", 
             hours, minutes, alarm_hour, alarm_minute);
             
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

esp_err_t settz_post_handler(httpd_req_t *req) {
    char buf[100];
    int ret, remaining = req->content_len;
    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            return ESP_FAIL;
        }
        remaining -= ret;
    }
    buf[req->content_len] = '\0';

    int hours = 0, minutes = 0;
    sscanf(buf, "tz_hours=%d&tz_minutes=%d", &hours, &minutes);
    
    // Limit to reasonable range (-12 to +14 hours, 0 or 30 minutes)
    if (hours >= -12 && hours <= 14 && (minutes == 0 || minutes == 30)) {
        timezone_hours = hours;
        timezone_minutes = minutes;
        
        // Save to NVS
        save_wifi_settings();
        
        // Set timezone - POSIX format for proper timezone handling
        char tz_str[32];
        // POSIX timezone format: For UTC+5:30, we need IST-5:30
        if (timezone_minutes == 0) {
            snprintf(tz_str, sizeof(tz_str), "IST-%d", timezone_hours);
        } else {
            snprintf(tz_str, sizeof(tz_str), "IST-%d:%02d", timezone_hours, timezone_minutes);
        }
        setenv("TZ", tz_str, 1);
        tzset();
        
        // Use pre-formatted string to avoid format truncation warning
        char log_buf[32];
        snprintf(log_buf, sizeof(log_buf), "%s", tz_str);
        ESP_LOGI(TAG, "Timezone set to %s", log_buf);
    }
    
    httpd_resp_sendstr(req, "Timezone updated");
    return ESP_OK;
}

esp_err_t dismiss_post_handler(httpd_req_t *req) {
    gpio_set_level(BUZZER_PIN, 0);
    alarm_triggered = false;
    ESP_LOGI(TAG, "Alarm dismissed by web interface.");
    httpd_resp_sendstr(req, "Alarm dismissed.");
    return ESP_OK;
}

esp_err_t setwifi_post_handler(httpd_req_t *req) {
    // Increase buffer size for form data and handle larger requests
    const size_t buf_size = 2048;  // Increased buffer size
    char *buf = malloc(buf_size);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for WiFi form data");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }
    
    int remaining = req->content_len;
    int received = 0;
    
    ESP_LOGI(TAG, "Receiving WiFi form data, content length: %d", remaining);
    
    // Ensure we don't exceed buffer size
    if (remaining >= buf_size) {
        ESP_LOGE(TAG, "Content too long: %d bytes (max: %zu)", remaining, buf_size - 1);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Form data too long");
        free(buf);
        return ESP_FAIL;
    }
    
    // Receive data in chunks if needed
    int ret = 0;
    while (remaining > 0) {
        int chunk_size = MIN(remaining, 512);  // Receive in smaller chunks
        ret = httpd_req_recv(req, buf + received, chunk_size);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Timeout receiving data, retrying...");
                continue;
            }
            ESP_LOGE(TAG, "Failed to receive form data: %d", ret);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            free(buf);
            return ESP_FAIL;
        }
        received += ret;
        remaining -= ret;
        ESP_LOGD(TAG, "Received %d bytes, %d remaining", ret, remaining);
    }
    buf[received] = '\0';
    
    ESP_LOGI(TAG, "Successfully received %d bytes of form data", received);
    
    // Parse form data
    char new_ssid[32] = {0};
    char new_password[64] = {0};
    
    ESP_LOGD(TAG, "Form data received: %s", buf);  // Debug log (will be filtered unless debug level enabled)
    
    // Simple parsing with better error handling
    char *ssid_start = strstr(buf, "ssid=");
    char *pass_start = strstr(buf, "password=");
    
    if (ssid_start) {
        ssid_start += 5; // Skip "ssid="
        char *ssid_end = strchr(ssid_start, '&');
        if (ssid_end) {
            int len = ssid_end - ssid_start;
            if (len > 0 && len < sizeof(new_ssid)) {
                strncpy(new_ssid, ssid_start, len);
                new_ssid[len] = '\0';
            }
        } else {
            // If no ampersand, this is the last parameter
            strlcpy(new_ssid, ssid_start, sizeof(new_ssid));
        }
        
        // Use the URL decoder
        char decoded_ssid[32] = {0};
        url_decode(new_ssid, decoded_ssid, sizeof(decoded_ssid));
        strlcpy(new_ssid, decoded_ssid, sizeof(new_ssid));
        ESP_LOGI(TAG, "Parsed SSID: %s", new_ssid);
    }
    
    if (pass_start) {
        pass_start += 9; // Skip "password="
        
        // Copy the password
        strlcpy(new_password, pass_start, sizeof(new_password));
        
        // Use the URL decoder
        char decoded_password[64] = {0};
        url_decode(new_password, decoded_password, sizeof(decoded_password));
        strlcpy(new_password, decoded_password, sizeof(new_password));
        ESP_LOGI(TAG, "Password received (length: %d)", strlen(new_password));
    }
    
    // Update WiFi settings if SSID is provided
    if (strlen(new_ssid) > 0) {
        // Prevent setting the SSID to "Clock" (our own AP name)
        if (strcmp(new_ssid, "Clock") == 0) {
            httpd_resp_sendstr(req, "Error: Cannot set home WiFi to 'Clock' as this would create a loop.");
            free(buf);
            return ESP_OK;
        }
        
        strlcpy(wifi_ssid, new_ssid, sizeof(wifi_ssid));
        strlcpy(wifi_password, new_password, sizeof(wifi_password));
        wifi_has_password = (strlen(new_password) >= 8);
        
        // Save to NVS
        save_wifi_settings();
        
        // Inform user
        httpd_resp_sendstr(req, "WiFi settings updated. The device will restart in 5 seconds...");
        
        // Schedule a restart
        ESP_LOGI(TAG, "WiFi settings changed to SSID: %s. Restarting in 5 seconds...", wifi_ssid);
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        esp_restart();
    } else {
        httpd_resp_sendstr(req, "Error: SSID is required");
    }
    
    free(buf);
    return ESP_OK;
}

esp_err_t syncntp_post_handler(httpd_req_t *req) {
    // Read POST data (if any)
    char buf[100];
    int ret, remaining = req->content_len;
    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            return ESP_FAIL;
        }
        remaining -= ret;
    }
    
    if (wifi_sta_connected) {
        // Start NTP sync in a separate task
        xTaskCreate(
            sync_time_with_ntp,
            "ntp_sync_task",
            4096,
            NULL,
            5,
            NULL);
        
        httpd_resp_sendstr(req, "Time synchronization started. The page will refresh in 5 seconds.");
    } else {
        httpd_resp_sendstr(req, "Error: Not connected to WiFi. Please connect to your home WiFi network first to enable NTP sync.");
    }
    
    return ESP_OK;
}

httpd_handle_t start_webserver(void) {
    // Increase buffer sizes to prevent "header too long" errors
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 16384;        // Quadruple the default stack size (4KB -> 16KB)
    config.max_uri_handlers = 15;     // Increase handler count
    config.max_resp_headers = 12;     // Increase max response headers
    config.recv_wait_timeout = 60;    // Longer timeout for slow clients
    config.send_wait_timeout = 60;    // Longer send timeout
    
    // IMPORTANT: Increase these values to handle larger form submissions and headers
    config.uri_match_fn = httpd_uri_match_wildcard; // More flexible URI matching
    config.max_open_sockets = 4;      // Within ESP32 LWIP limits (max 7, 3 used internally = 4 available)
    config.backlog_conn = 5;          // Increase backlog connections
    config.lru_purge_enable = true;   // Enable LRU purge for better memory management
    
    httpd_handle_t server = NULL;
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error starting server: %s", esp_err_to_name(ret));
        return NULL;
    }

    httpd_uri_t get_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_handler
    };
    httpd_register_uri_handler(server, &get_uri);

    // Add handler for /meta.json to prevent 404 errors
    httpd_uri_t meta_json_uri = {
        .uri = "/meta.json",
        .method = HTTP_GET,
        .handler = meta_json_handler
    };
    httpd_register_uri_handler(server, &meta_json_uri);

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
    
    httpd_uri_t setcountdown_post_uri = {
        .uri = "/setcountdown",
        .method = HTTP_POST,
        .handler = setcountdown_post_handler
    };
    httpd_register_uri_handler(server, &setcountdown_post_uri);
    
    httpd_uri_t settz_post_uri = {
        .uri = "/settz",
        .method = HTTP_POST,
        .handler = settz_post_handler
    };
    httpd_register_uri_handler(server, &settz_post_uri);

    httpd_uri_t dismiss_post_uri = {
        .uri = "/dismiss",
        .method = HTTP_POST,
        .handler = dismiss_post_handler
    };
    httpd_register_uri_handler(server, &dismiss_post_uri);
    
    httpd_uri_t setwifi_post_uri = {
        .uri = "/setwifi",
        .method = HTTP_POST,
        .handler = setwifi_post_handler
    };
    httpd_register_uri_handler(server, &setwifi_post_uri);

    httpd_uri_t syncntp_post_uri = {
        .uri = "/syncntp",
        .method = HTTP_POST,
        .handler = syncntp_post_handler
    };
    httpd_register_uri_handler(server, &syncntp_post_uri);

    ESP_LOGI(TAG, "Web server started successfully");
    return server;
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
            wifi_sta_connected = true;
            ESP_LOGI(TAG, "Connected to home WiFi network");
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_sta_connected = false;
            ESP_LOGI(TAG, "Disconnected from home WiFi network");
            
            // Only attempt to reconnect if no AP clients and not already trying to reconnect
            if (ap_client_count == 0 && !reconnect_timer_active && strlen(wifi_ssid) > 0) {
                start_wifi_reconnect_timer();
            }
        } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            // Increment connected client count
            ap_client_count++;
            ESP_LOGI(TAG, "Station connected to AP");
            
            // Disconnect from home WiFi if we're connected
            if (wifi_sta_connected) {
                ESP_LOGI(TAG, "Disconnecting from home WiFi while client is connected to AP");
                esp_wifi_disconnect();
            }
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            // Decrement connected client count
            if (ap_client_count > 0) {
                ap_client_count--;
            }
            ESP_LOGI(TAG, "Station disconnected from AP");
            
            // If no clients connected to AP, reconnect to home WiFi after a delay
            if (ap_client_count == 0 && !reconnect_timer_active && strlen(wifi_ssid) > 0) {
                start_wifi_reconnect_timer();
            }
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
            
            // Start NTP sync once we have an IP
            start_periodic_ntp_sync();
        }
    }
}

void wifi_init_softap(void) {
    // Create default event loop
    esp_netif_t *ap_netif __attribute__((unused)) = esp_netif_create_default_wifi_ap();
    esp_netif_t *sta_netif __attribute__((unused)) = esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Load WiFi settings from NVS
    load_wifi_settings();
    
    // Configure AP mode with password (must be at least 8 characters for WPA2)
    wifi_config_t ap_config = {
        .ap = {
            .max_connection = 4,
            .channel = 1,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .ssid_hidden = 0,
        }
    };
    
    // Copy SSID and set length for AP mode - always use "Clock" for AP
    strlcpy((char*)ap_config.ap.ssid, "Clock", sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen("Clock");

    // Set AP password (must be at least 8 chars for WPA2)
    // Change "clockpass" to your desired password (minimum 8 characters)
    strlcpy((char*)ap_config.ap.password, "clockpass", sizeof(ap_config.ap.password));
    
    // Set up AP mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started with SSID: Clock");
    ESP_LOGI(TAG, "WiFi AP security: WPA2-PSK, password: %s", ap_config.ap.password);

    // If we have home WiFi credentials, try to connect after a delay
    if (strlen(wifi_ssid) > 0 && wifi_has_password) {
        // Schedule WiFi reconnect after AP is established
        start_wifi_reconnect_timer();
    }
}

void app_main(void) {
    // Initialize NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "Starting ESP32 WROOM-32D Clock application");
    
    // Reset saved WiFi settings if needed to prevent self-connection
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        // Check if SSID is "Clock" and if so, clear it
        size_t required_size = sizeof(wifi_ssid);
        char temp_ssid[32] = {0};
        if (nvs_get_str(my_handle, "wifi_ssid", temp_ssid, &required_size) == ESP_OK) {
            if (strcmp(temp_ssid, "Clock") == 0) {
                ESP_LOGI(TAG, "Found 'Clock' as saved WiFi SSID - clearing to prevent self-connection");
                nvs_erase_key(my_handle, "wifi_ssid");
                nvs_erase_key(my_handle, "wifi_pass");
                nvs_erase_key(my_handle, "wifi_has_pass");
                nvs_commit(my_handle);
            }
        }
        nvs_close(my_handle);
    }
    
    esp_netif_init();
    esp_event_loop_create_default();
    
    // Register WiFi event handlers
    esp_event_handler_instance_t instance_wifi_event;
    esp_event_handler_instance_t instance_ip_event;
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                       ESP_EVENT_ANY_ID,
                                                       &wifi_event_handler,
                                                       NULL,
                                                       &instance_wifi_event));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                       IP_EVENT_STA_GOT_IP,
                                                       &wifi_event_handler,
                                                       NULL,
                                                       &instance_ip_event));

    // Initialize WiFi
    wifi_init_softap();
    
    // Initialize MAX7219 display
    max7219_init();
    
    // Test the display (comment out after verification)
    test_display();
    
    // Start web server
    start_webserver();

    // Configure buzzer pin
    gpio_config_t buzzer_conf = {
        .pin_bit_mask = (1ULL << BUZZER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&buzzer_conf);

    // Configure dismiss button (GPIO0 - BOOT button)
    gpio_config_t dismiss_button_conf = {
        .pin_bit_mask = (1ULL << DISMISS_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,  // GPIO0 has external pull-up
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&dismiss_button_conf);

    // Configure seconds indicator LED pin
    gpio_config_t seconds_led_conf = {
        .pin_bit_mask = (1ULL << SECONDS_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&seconds_led_conf);

    // Configure AM/PM indicator LED pin
    gpio_config_t ampm_led_conf = {
        .pin_bit_mask = (1ULL << AMPM_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&ampm_led_conf);

    // Set timezone for Sri Lanka using POSIX format
    char tz_str[32];
    // POSIX timezone format: For UTC+5:30, we need IST-5:30
    if (timezone_minutes == 0) {
        snprintf(tz_str, sizeof(tz_str), "IST-%d", timezone_hours);
    } else {
        snprintf(tz_str, sizeof(tz_str), "IST-%d:%02d", timezone_hours, timezone_minutes);
    }
    setenv("TZ", tz_str, 1);
    tzset();
    
    // Pre-format timezone string to avoid truncation warning
    char log_tz_str[64];
    snprintf(log_tz_str, sizeof(log_tz_str), "IST-%d:%02d (Sri Lanka/IST UTC+%d:%02d)", 
             timezone_hours, timezone_minutes, timezone_hours, timezone_minutes);
    ESP_LOGI(TAG, "Timezone set to %s", log_tz_str);

    // Set initial time if not set
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // If time is not set, set to a reasonable current time
    if (timeinfo.tm_year < (2020 - 1900)) {
        // Set to current date from context (June 27, 2025) with default time
        struct tm default_time = {
            .tm_year = 125,  // 2025 - 1900
            .tm_mon = 5,     // June (0-based)
            .tm_mday = 27,   // Updated to current date
            .tm_hour = 12,   // Noon
            .tm_min = 0,
            .tm_sec = 0
        };
        time_t default_time_t = mktime(&default_time);
        struct timeval tv = { .tv_sec = default_time_t };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "Set initial time to 2025-06-27 12:00:00");
    }

    ESP_LOGI(TAG, "Clock initialized and running");
    
    // Main loop
    while (1) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        display_time(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        // Debug output every 10 seconds
        static int debug_counter = 0;
        if (debug_counter++ >= 100) { // Every 10 seconds (100 * 100ms)
            ESP_LOGI(TAG, "Current time: %04d-%02d-%02d %02d:%02d:%02d",
                    timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            ESP_LOGI(TAG, "WiFi status - STA: %s, AP clients: %d", 
                    wifi_sta_connected ? "connected" : "disconnected", ap_client_count);
            if (alarm_hour >= 0 && alarm_minute >= 0) {
                ESP_LOGI(TAG, "Alarm set for: %02d:%02d (triggered: %s)", 
                        alarm_hour, alarm_minute, alarm_triggered ? "yes" : "no");
            }
            debug_counter = 0;
        }

        // Blink LED on each second change
        if (timeinfo.tm_sec != last_second) {
            // Toggle LED state
            static bool led_state = false;
            led_state = !led_state;
            gpio_set_level(SECONDS_LED_PIN, led_state);
            last_second = timeinfo.tm_sec;
        }

        // Check for time interval notifications (only when seconds = 0)
        if (timeinfo.tm_sec == 0) {
            // Check for hour change (at the top of each hour - XX:00:00)
            if (timeinfo.tm_hour != last_hour && timeinfo.tm_min == 0) {
                // Only if we're not already in an alarm state
                if (!alarm_triggered) {
                    ESP_LOGI(TAG, "Hour completed! Two beeps.");
                    double_beep();
                }
            }
            
            // Check for half-hour (XX:30:00)
            if (timeinfo.tm_min == 30 && timeinfo.tm_min != last_minute) {
                // Only if we're not already in an alarm state
                if (!alarm_triggered) {
                    ESP_LOGI(TAG, "Half hour completed! One beep.");
                    single_beep();
                }
            }
        }
        
        // Update time tracking variables
        last_hour = timeinfo.tm_hour;
        last_minute = timeinfo.tm_min;

        // Existing alarm checking code
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

        // Check dismiss button with debouncing
        bool current_button_state = gpio_get_level(DISMISS_BUTTON_PIN);
        uint32_t current_time_ms = pdTICKS_TO_MS(xTaskGetTickCount());
        
        if (current_button_state != last_button_state) {
            last_button_change = current_time_ms;
        }
        
        if ((current_time_ms - last_button_change) > DEBOUNCE_DELAY_MS) {
            if (current_button_state == 0 && last_button_state == 1) {
                // Button was just pressed (falling edge)
                gpio_set_level(BUZZER_PIN, 0);
                alarm_triggered = false;
                ESP_LOGI(TAG, "Alarm dismissed by button.");
            }
        }
        
        last_button_state = current_button_state;

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Debug function to test MAX7219 display
void test_display(void) {
    ESP_LOGI(TAG, "Testing MAX7219 display...");
    
    // Test all segments
    for (int digit = 1; digit <= 6; digit++) {
        max7219_send(digit, 0xFF); // All segments on
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // Clear all digits
    for (int digit = 1; digit <= 6; digit++) {
        max7219_send(digit, 0x00);
    }
    
    // Test individual digits
    for (int i = 0; i <= 9; i++) {
        for (int digit = 1; digit <= 6; digit++) {
            max7219_send(digit, digit_to_segment[i]);
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    
    ESP_LOGI(TAG, "Display test completed");
}