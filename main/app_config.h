/*
 * app_config.h
 *
 *  Created on: Jul 9, 2025
 *      Author: GitHub Copilot
 */

#ifndef MAIN_APP_CONFIG_H_
#define MAIN_APP_CONFIG_H_

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_http_server.h"
#include "esp_sntp.h"
#include "esp_timer.h"

// Pin definitions for ESP32 WROOM-32D
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5
#define BUZZER_PIN   4
#define DISMISS_BUTTON_PIN 0 // GPIO0 is the BOOT button
#define SECONDS_LED_PIN 2
#define AMPM_LED_PIN 19

// Wi-Fi AP credentials
#define WIFI_AP_SSID "ESP32_Clock"
#define WIFI_AP_PASSWORD "12345678"

// Global variables (declared as extern)
extern spi_device_handle_t spi;
extern int alarm_hour;
extern int alarm_minute;
extern bool alarm_triggered;
extern int timezone_hours;
extern int timezone_minutes;
extern char wifi_ssid[32];
extern char wifi_password[64];
extern bool wifi_has_password;
extern bool wifi_sta_connected;
extern int ap_client_count;
extern bool reconnect_timer_active;
extern SemaphoreHandle_t wifi_semaphore;

#endif /* MAIN_APP_CONFIG_H_ */
