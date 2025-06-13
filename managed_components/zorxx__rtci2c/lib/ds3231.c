/*! \copyright 2024 Zorxx Software. All rights reserved.
 *  \license This file is released under the MIT License. See the LICENSE file for details.
 *  \brief ds3231 real-time clock device interface
 * The majority of this implementation can be found in ds1307.c
 */
#include "helpers.h"
#include "rtci2c_private.h"
#include "ds3231.h"
#include "ds1307.h"

/* ----------------------------------------------------------------------------------------------
 * Device-specific Implementation Functions 
 */

static bool ds3231_init(void *rtci2c_ctx)
{
   rtci2c_t *r = (rtci2c_t *) rtci2c_ctx;
   uint8_t data[16]; 

   if(!i2c_ll_read_reg(r->lowlevel, DS1307_REG_SECONDS, data, sizeof(data)))
   {
      SERR("[%s] Failed to query RTC", __func__);
      return false;
   }
   SDBG("[%s] Control = 0x%02x, Status = 0x%02x", __func__, data[0], data[1]);

   if(DS3231_REG_GET_BIT(data, CONTROL, CONTROL_EOSC_BIT) != 0)
   {
      SDBG("[%s] Oscillator stopped; starting", __func__);
      data[0] &= ~(1 << DS3231_REG_CONTROL_EOSC_BIT);
      if(!i2c_ll_write_reg(r->lowlevel, DS3231_REG_CONTROL, data, 1))
      {
         SERR("[%s] Failed to enable oscillator", __func__);
      }
   }

   data[0] = 0; /* clear */
   if(!i2c_ll_write_reg(r->lowlevel, DS3231_REG_STATUS, data, 1))
   {
      SERR("[%s] Failed to clear status", __func__);
   }

   SDBG("[%s] Success", __func__);
   return true;
}

/* ----------------------------------------------------------------------------------------------
 * Exported Functions 
 */

bool ds3231_configure(rtci2c_t *ctx, uint8_t i2c_address)
{
   ctx->i2c_address = DS1307_I2C_ADDRESS; /* there is only one */
   ctx->i2c_max_transfer_length = DS1307_I2C_MAX_TRANSFER_LENGTH;
   ctx->i2c_speed = DS1307_I2C_SPEED;
   ctx->devfn_init = ds3231_init;
   ctx->devfn_get_datetime = ds1307_get_datetime;
   ctx->devfn_set_datetime = ds1307_set_datetime;
   return true;
}
