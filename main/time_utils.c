#include "time_utils.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <string.h>
#include <sys/time.h>
#include "app_config.h"

static const char *TAG = "TIME_UTILS";
struct tm current_time;

static void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

void sync_time(void) {
    time_utils_init_sntp();
    // wait for time to be set
    time_t now = 0;
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    time(&now);
    localtime_r(&now, &current_time);
}

void update_time(void) {
    time_t now;
    time(&now);
    localtime_r(&now, &current_time);
}

void time_utils_init_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

void time_utils_obtain_time(void) {
    time_utils_init_sntp();
    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    time(&now);
    localtime_r(&now, &timeinfo);
}

void time_utils_set_system_time(const char* tzid) {
    setenv("TZ", tzid, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to: %s", tzid);
}

void time_utils_set_time_from_string(const char* time_str) {
    if (time_str == NULL) return;

    struct tm tm;
    if (sscanf(time_str, "%d:%d", &tm.tm_hour, &tm.tm_min) == 2) {
        tm.tm_sec = 0;

        time_t t;
        time(&t); // Get current time to preserve date
        struct tm *now = localtime(&t);

        now->tm_hour = tm.tm_hour;
        now->tm_min = tm.tm_min;
        now->tm_sec = tm.tm_sec;

        time_t new_time = mktime(now);
        struct timeval tv = { .tv_sec = new_time, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "Time set to: %02d:%02d", tm.tm_hour, tm.tm_min);
    }
}
