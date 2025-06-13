/*! \copyright 2024 Zorxx Software. All rights reserved.
 *  \license This file is released under the MIT License. See the LICENSE file for details.
 *  \brief rtci2c library esp-idf example application
 */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "rtci2c/rtci2c.h"

#define TAG "RTCI2C"

#define RTCI2C_LIBRARY_I2C_BUS_INIT 0

/* The following definitions may change, based on the ESP device,
   RTC device configuration, and wiring between them. */
#define ESP_I2C_PORT I2C_NUM_0
#define ESP_I2C_SDA  GPIO_NUM_21
#define ESP_I2C_SCL  GPIO_NUM_22
#define DEVICE_I2C_ADDRESS 0 /* let the library figure it out */
static i2c_master_bus_handle_t i2c_bus;

void app_main(void)
{
   i2c_lowlevel_config config = {0}; /* ensure initialize to zero */

   ESP_ERROR_CHECK(nvs_flash_init());
   ESP_ERROR_CHECK(esp_event_loop_create_default() );

   #if RTCI2C_LIBRARY_I2C_BUS_INIT
   /* Allow the rtci2c library to initialize the i2c bus */
   config.port = ESP_I2C_PORT;
   config.pin_sda = ESP_I2C_SDA;
   config.pin_scl = ESP_I2C_SCL;

   #else
   /* Initialize the i2c bus the way we want. Optionally keep the "bus"
      pointer to allow the use of the same bus instance for other i2c
      device communication. */
   i2c_master_bus_config_t bus_cfg = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = ESP_I2C_PORT, 
      .sda_io_num = ESP_I2C_SDA,
      .scl_io_num = ESP_I2C_SCL,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,      
   };
   if(i2c_new_master_bus(&bus_cfg, &i2c_bus) != ESP_OK)
   {
      ESP_LOGE(TAG, "Failed to initialize I2C bus");
   }
   config.bus = &i2c_bus;
   #endif

   rtci2c_context *ctx = rtci2c_init(RTCI2C_DEVICE_DS1307, DEVICE_I2C_ADDRESS, &config);
   if(NULL == ctx)
   {
      ESP_LOGE(TAG, "Initialization failed");
   }
   else
   {
      struct tm t;

      for(int i = 0; i < 100; ++i)
      {
         if(!rtci2c_get_datetime(ctx, &t))
         {
            ESP_LOGE(TAG, "Date/tate query failed");
         }
         else
         {
            ESP_LOGI(TAG, "Current: %02u/%02u/%u %u:%02u:%02u",
               t.tm_mday, t.tm_mon, t.tm_year, t.tm_hour, t.tm_min, t.tm_sec);
         }
         vTaskDelay(pdMS_TO_TICKS(100));
      }
      rtci2c_deinit(ctx);
   }

   ESP_LOGI(TAG, "Test application finished");

   for(;;)
      vTaskDelay(portMAX_DELAY);
}
