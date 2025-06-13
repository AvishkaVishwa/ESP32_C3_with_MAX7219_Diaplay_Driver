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
#include "esp_sntp.h"  // Use ESP-IDF's SNTP implementation
#include "esp_timer.h" // For periodic timer

#define TAG "CLOCK"

// MAX7219 SPI configuration
#define PIN_NUM_MOSI 6
#define PIN_NUM_CLK  4
#define PIN_NUM_CS   7

// Buzzer pin and Dismiss button pin
#define BUZZER_PIN   3
#define DISMISS_BUTTON_PIN 9

// LED pin for seconds indicator
#define SECONDS_LED_PIN 10

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

// Sri Lanka timezone (IST - UTC+5:30)
static int timezone_hours = 5;    // Hours offset from UTC
static int timezone_minutes = 30;  // Minutes offset

// WiFi settings
static char wifi_ssid[32] = "";
static char wifi_password[64] = "";
static bool wifi_has_password = false;
static bool wifi_sta_connected = false;
static int ap_client_count = 0;
static bool reconnect_timer_active = false;
static esp_timer_handle_t reconnect_timer = NULL;

// Forward declarations for new functions
void ntp_timer_callback(void* arg);
void start_periodic_ntp_sync(void);
void wifi_reconnect_timer_callback(void* arg);
void start_wifi_reconnect_timer(void);
void reconnect_to_home_wifi(void);

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

// Time sync notification callback
void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

// Function to synchronize time with NTP servers
void sync_time_with_ntp(void* pvParameters) {
    ESP_LOGI(TAG, "Initializing SNTP");
    
    // Set timezone for Sri Lanka (IST)
    char tz_str[32];
    if (timezone_minutes == 0) {
        snprintf(tz_str, sizeof(tz_str), "UTC%+d", timezone_hours);
    } else {
        snprintf(tz_str, sizeof(tz_str), "UTC%+d:%02d", timezone_hours, timezone_minutes);
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
void save_wifi_settings() {
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

void load_wifi_settings() {
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
    
    // Fix for line 490 - pre-format the timezone string
    char tz_str[64]; // Increased buffer size to prevent truncation
    snprintf(tz_str, sizeof(tz_str), "UTC%+d:%02d", timezone_hours, timezone_minutes);
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
        "<h1>ESP32-C3 Clock</h1>");

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

    snprintf(tz_chunk, sizeof(tz_chunk), "%+d:%02d</p></form>", timezone_hours, timezone_minutes);
    httpd_resp_sendstr_chunk(req, tz_chunk);

    // WiFi form - Break into smaller chunks to avoid buffer overflow
    httpd_resp_sendstr_chunk(req, 
        "<h2>WiFi Settings</h2>"
        "<form action='/setwifi' method='post'>"
        "<p>The clock creates its own 'Clock' network for configuration, but can also connect to your home WiFi for internet time sync.</p>"
        "<fieldset>"
        "<legend>Home WiFi Connection</legend>");

    // Format just the input fields with dynamic values
    snprintf(chunk, sizeof(chunk), 
        "Network Name: <input type='text' name='ssid' value='%s'><br><br>"
        "Password: <input type='password' name='password' placeholder='WiFi password'><br>",
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
        .tm_year = 125, // 2025 - 1900
        .tm_mon = 5,    // June (0-based)
        .tm_mday = 13,
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
        
        // Set timezone
        char tz_str[32];
        if (timezone_minutes == 0) {
            snprintf(tz_str, sizeof(tz_str), "UTC%+d", timezone_hours);
        } else {
            snprintf(tz_str, sizeof(tz_str), "UTC%+d:%02d", timezone_hours, timezone_minutes);
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
    char buf[256];
    int ret, remaining = req->content_len;
    
    // Buffer to collect request data
    if (remaining >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
        return ESP_FAIL;
    }
    
    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        remaining -= ret;
    }
    buf[req->content_len] = '\0';
    
    // Parse form data
    char new_ssid[32] = {0};
    char new_password[64] = {0};
    
    // Simple parsing (you might want to use a more robust method)
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
        
        // URL-decode the SSID
        for (int i = 0; i < strlen(new_ssid); i++) {
            if (new_ssid[i] == '+') {
                new_ssid[i] = ' ';
            }
        }
    }
    
    if (pass_start) {
        pass_start += 9; // Skip "password="
        // URL-decode and copy the password
        strlcpy(new_password, pass_start, sizeof(new_password));
        
        // URL-decode the password
        for (int i = 0; i < strlen(new_password); i++) {
            if (new_password[i] == '+') {
                new_password[i] = ' ';
            }
        }
    }
    
    // Update WiFi settings if SSID is provided
    if (strlen(new_ssid) > 0) {
        strlcpy(wifi_ssid, new_ssid, sizeof(wifi_ssid));
        strlcpy(wifi_password, new_password, sizeof(wifi_password));
        wifi_has_password = (strlen(new_password) >= 8);
        
        // Save to NVS
        save_wifi_settings();
        
        // Inform user
        httpd_resp_sendstr(req, "WiFi settings updated. The device will restart in 5 seconds...");
        
        // Schedule a restart
        ESP_LOGI(TAG, "WiFi settings changed. Restarting in 5 seconds...");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        esp_restart();
        return ESP_OK;
    }
    
    httpd_resp_sendstr(req, "Error: SSID is required");
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

httpd_handle_t start_webserver() {
    // Increase stack size to prevent stack overflow
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;  // Double the default stack size
    
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
            if (ap_client_count == 0 && !reconnect_timer_active) {
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
            if (ap_client_count == 0 && !reconnect_timer_active) {
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

void wifi_init_softap() {
    // Create default event loop
    esp_netif_t *ap_netif __attribute__((unused)) = esp_netif_create_default_wifi_ap();
    esp_netif_t *sta_netif __attribute__((unused)) = esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    
    // Load WiFi settings from NVS
    load_wifi_settings();
    
    // Configure AP mode
    wifi_config_t ap_config = {
        .ap = {
            .max_connection = 4,
            .channel = 1
        }
    };
    
    // Copy SSID and set length for AP mode - always use "Clock" for AP
    strlcpy((char*)ap_config.ap.ssid, "Clock", sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen("Clock");

    // Set AP password to "1" (minimum 8 chars required for WPA2)
    // If you want WPA2, use at least 8 chars, e.g., "11111111"
    strlcpy((char*)ap_config.ap.password, "11111111", sizeof(ap_config.ap.password));
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    ap_config.ap.ssid_hidden = 0;
    ap_config.ap.max_connection = 4;

    // First, just set up AP mode
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);

    esp_wifi_start();

    ESP_LOGI(TAG, "WiFi AP started with SSID: Clock");
    ESP_LOGI(TAG, "WiFi AP security: WPA/WPA2-PSK, password: %s", ap_config.ap.password);

    // If we have home WiFi credentials, try to connect after a delay
    if (strlen(wifi_ssid) > 0 && wifi_has_password) {
        // Schedule WiFi reconnect after AP is established
        start_wifi_reconnect_timer();
    }
}

void app_main() {
    // Initialize NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
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

    // Configure dismiss button
    gpio_config_t dismiss_button_conf = {
        .pin_bit_mask = (1ULL << DISMISS_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
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

    // Set timezone for Sri Lanka
    char tz_str[32];
    if (timezone_minutes == 0) {
        snprintf(tz_str, sizeof(tz_str), "UTC%+d", timezone_hours);
    } else {
        snprintf(tz_str, sizeof(tz_str), "UTC%+d:%02d", timezone_hours, timezone_minutes);
    }
    setenv("TZ", tz_str, 1);
    tzset();
    
    // Pre-format timezone string to avoid truncation warning
    char log_tz_str[64];
    snprintf(log_tz_str, sizeof(log_tz_str), "%s (Sri Lanka/IST)", tz_str);
    ESP_LOGI(TAG, "Timezone set to %s", log_tz_str);

    // Set initial time if not set
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // If time is not set, set to the current time from the message
    if (timeinfo.tm_year < (2020 - 1900)) {
        // Set to 2025-06-13 10:15:51
        struct tm default_time = {
            .tm_year = 125,  // 2025 - 1900
            .tm_mon = 5,     // June (0-based)
            .tm_mday = 13,
            .tm_hour = 10,
            .tm_min = 15,
            .tm_sec = 51
        };
        time_t default_time_t = mktime(&default_time);
        struct timeval tv = { .tv_sec = default_time_t };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "Set initial time to 2025-06-13 10:15:51");
    }

    // Main loop
    int last_second = -1;  // Track previous second for LED blinking
    while (1) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        display_time(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        // Blink LED on each second change
        if (timeinfo.tm_sec != last_second) {
            // Toggle LED state
            static bool led_state = false;
            led_state = !led_state;
            gpio_set_level(SECONDS_LED_PIN, led_state);
            last_second = timeinfo.tm_sec;
        }

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

        vTaskDelay(pdMS_TO_TICKS(100)); // Check more frequently to ensure responsive LED blinking
    }
}