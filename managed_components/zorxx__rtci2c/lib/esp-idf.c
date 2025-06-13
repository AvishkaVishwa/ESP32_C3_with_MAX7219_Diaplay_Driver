/*! \copyright 2024 Zorxx Software. All rights reserved.
 *  \license This file is released under the MIT License. See the LICENSE file for details.
 *  \brief esp-idf portability implementation 
 */
#include <string.h>  /* memcpy */
#include "freertos/FreeRTOS.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "sys_esp.h"
#include "sys.h"
#include "helpers.h"

typedef struct
{
   i2c_lowlevel_config config;
   i2c_master_bus_handle_t bus;
   bool bus_created;
   i2c_master_dev_handle_t device;
   uint32_t timeout;
} esp_i2c_t;

typedef struct
{
   SemaphoreHandle_t mutex;
} esp_mutex_t;

/* ----------------------------------------------------------------------------------------------
 * I2C low-level implementation for esp-idf 
 */

i2c_lowlevel_context SYS_WEAK i2c_ll_init(uint8_t i2c_address, uint32_t i2c_speed, uint32_t i2c_timeout_ms,
                                      i2c_lowlevel_config *config)
{
   i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = i2c_address,
      .scl_speed_hz = i2c_speed,
   };

   esp_i2c_t *l = (esp_i2c_t *) calloc(1, sizeof(*l));
   if(NULL == l)
      return NULL; 
   memcpy(&l->config, config, sizeof(l->config));
   l->timeout = i2c_timeout_ms;

   if(NULL == config->bus)
   {
      i2c_master_bus_config_t bus_cfg = {
         .clk_source = I2C_CLK_SRC_DEFAULT,
         .i2c_port = config->port,
         .sda_io_num = config->pin_sda,
         .scl_io_num = config->pin_scl,
         .glitch_ignore_cnt = 7,
         .flags.enable_internal_pullup = true,      
      };
      if(i2c_new_master_bus(&bus_cfg, &l->bus) != ESP_OK)
      {
         SERR("Failed to initialize I2C bus");
         free(l);
         return NULL;
      }
      l->config.bus = &l->bus;
      l->bus_created = true;
   }
   else
      l->bus_created = false;

   if(i2c_master_bus_add_device(*l->config.bus, &dev_cfg, &l->device) != ESP_OK)
   {
      SERR("I2C initialization failed");
      free(l);
      return NULL;
   }

   return (i2c_lowlevel_context) l;
}

bool SYS_WEAK i2c_ll_deinit(i2c_lowlevel_context ctx)
{
   esp_i2c_t *l = (esp_i2c_t *) ctx;
   if(l->bus_created)
      i2c_del_master_bus(l->bus);
   free(l);
   return true;
}

bool SYS_WEAK i2c_ll_write(i2c_lowlevel_context ctx, uint8_t *data, uint8_t length)
{
   esp_i2c_t *l = (esp_i2c_t *) ctx;
   return (i2c_master_transmit(l->device, data, length, -1) == ESP_OK);
}

bool SYS_WEAK i2c_ll_write_reg(i2c_lowlevel_context ctx, uint8_t reg, uint8_t *data, uint8_t length)
{
   esp_i2c_t *l = (esp_i2c_t *) ctx;
   uint8_t *buffer;
   esp_err_t result;

   buffer = (uint8_t *) malloc(length + 1);
   if(NULL == buffer)
      return false;

   buffer[0] = reg;
   memcpy(&buffer[1], data, length);
   result = i2c_master_transmit(l->device, buffer, length+1, -1);
   free(buffer);

   return (result == ESP_OK);
}

bool SYS_WEAK i2c_ll_read(i2c_lowlevel_context ctx, uint8_t *data, uint8_t length)
{
   esp_i2c_t *l = (esp_i2c_t *) ctx;
   return (i2c_master_receive(l->device, data, length, -1) == ESP_OK);
}

bool SYS_WEAK i2c_ll_read_reg(i2c_lowlevel_context ctx, uint8_t reg, uint8_t *data, uint8_t length)
{
   esp_i2c_t *l = (esp_i2c_t *) ctx;
   return (i2c_master_transmit_receive(l->device, &reg, 1, data, length, -1) == ESP_OK);
}

mutex_lowlevel SYS_WEAK sys_mutex_init(void)
{
   esp_mutex_t *ctx = malloc(sizeof(*ctx));
   if(NULL == ctx)
      return NULL;
   ctx->mutex = xSemaphoreCreateMutex();
   if(NULL == ctx->mutex)
   {
      free(ctx);
      return NULL;
   }
   return ctx;
}

bool SYS_WEAK sys_mutex_deinit(mutex_lowlevel mutex)
{
   esp_mutex_t *ctx = (esp_mutex_t *) mutex;
   if(NULL == ctx)
      return true;
   free(ctx);
   return true;
}

bool SYS_WEAK sys_mutex_lock(mutex_lowlevel mutex)
{
   esp_mutex_t *ctx = (esp_mutex_t *) mutex;
   xSemaphoreTake(ctx->mutex, portMAX_DELAY);
   return true;
}

bool SYS_WEAK sys_mutex_unlock(mutex_lowlevel mutex)
{
   esp_mutex_t *ctx = (esp_mutex_t *) mutex;
   xSemaphoreGive(ctx->mutex);
   return true;
}

uint64_t SYS_WEAK sys_microsecond_tick(void)
{
   return esp_timer_get_time(); /* microseconds since boot */
}
