#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

extern bool wifi_connected;

// Function prototypes for WiFi management
void wifi_manager_init(void);
void wifi_manager_start_ap(void);
esp_err_t wifi_manager_connect_to_ap(void);
bool wifi_manager_load_sta_config(void);

#endif // WIFI_MANAGER_H
