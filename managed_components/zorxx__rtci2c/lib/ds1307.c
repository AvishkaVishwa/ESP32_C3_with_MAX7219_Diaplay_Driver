/*! \copyright 2024 Zorxx Software. All rights reserved.
 *  \license This file is released under the MIT License. See the LICENSE file for details.
 *  \brief ds1307 real-time clock device interface
 */
#include "helpers.h"
#include "rtci2c_private.h"
#include "ds1307.h"

/* ----------------------------------------------------------------------------------------------
 * Device-specific Implementation Functions 
 */

bool ds1307_get_datetime(void *rtci2c_ctx, struct tm *datetime)
{
   rtci2c_t *r = (rtci2c_t *) rtci2c_ctx;
   uint8_t data[8];

   if(NULL == datetime)
      return false;

   if(!i2c_ll_read_reg(r->lowlevel, DS1307_REG_SECONDS, data, sizeof(data)))
   {
      SERR("Failed to query RTC");
      return false;
   }

   datetime->tm_sec = DS1307_REG_GET(data, SECONDS);
   datetime->tm_min = DS1307_REG_GET(data, MINUTES);
   datetime->tm_mday = DS1307_REG_GET(data, DAYOFMONTH);
   datetime->tm_wday = (DS1307_REG_GET(data, DAYOFWEEK) - 1) % 7;
   datetime->tm_mon = (DS1307_REG_GET(data, MONTH) - 1) % 12;
   datetime->tm_year = DS1307_REG_GET(data, YEAR) + 100;

   if(DS1307_REG_GET_BIT(data, HOURS, HOURS_24_BIT) == 0)
      datetime->tm_hour = DS1307_REG_GET(data, HOURS_24);
   else
   {
      bool ispm = DS1307_REG_GET_BIT(data, HOURS, HOURS_AMPM_BIT);
      datetime->tm_hour = DS1307_REG_GET(data, HOURS_12) + ((ispm) ? 12 : 0);
   }

   return true;
}

bool ds1307_set_datetime(void *rtci2c_ctx, struct tm *datetime)
{
   rtci2c_t *r = (rtci2c_t *) rtci2c_ctx;
   uint8_t data[8] = {0};

   if(NULL == datetime)
      return false;

   data[DS1307_REG_SECONDS]    = RTC_DEC_TO_BCD(datetime->tm_sec % 60);
   data[DS1307_REG_MINUTES]    = RTC_DEC_TO_BCD(datetime->tm_min % 60);
   data[DS1307_REG_HOURS]      = RTC_DEC_TO_BCD(datetime->tm_hour % 24); /* always set in 24-hour mode */
   data[DS1307_REG_DAYOFWEEK]  = RTC_DEC_TO_BCD((datetime->tm_wday % 7) + 1);
   data[DS1307_REG_DAYOFMONTH] = RTC_DEC_TO_BCD(datetime->tm_mday % 32);
   data[DS1307_REG_MONTH]      = RTC_DEC_TO_BCD((datetime->tm_mon % 12) + 1);
   data[DS1307_REG_YEAR]       = RTC_DEC_TO_BCD(datetime->tm_year % 100);

   if(!i2c_ll_write_reg(r->lowlevel, DS1307_REG_SECONDS, data, sizeof(data)))
   {
      SERR("Failed to set RTC");
      return false;
   }

   return true;
}

static bool ds1307_init(void *rtci2c_ctx)
{
   rtci2c_t *r = (rtci2c_t *) rtci2c_ctx;
   uint8_t data[8]; 

   if(!i2c_ll_read_reg(r->lowlevel, DS1307_REG_SECONDS, data, sizeof(data)))
   {
      SERR("[%s] Failed to query RTC", __func__);
      return false;
   }

   if(DS1307_REG_GET_BIT(data, SECONDS, SECONDS_CS_BIT))
   {
      SDBG("[%s] Clock stopped; starting", __func__);
      data[0] = 0; /* data */
      if(!i2c_ll_write_reg(r->lowlevel, DS1307_REG_SECONDS, data, 1))
      {
         SERR("[%s] Failed to enable clock", __func__);
      }
   }

   if(DS1307_REG_GET_BIT(data, CONTROL, CONTROL_OUT_BIT))
   {
      SDBG("[%s] Disabling square-wave output", __func__);
      data[0] = 0; /* data */
      if(!i2c_ll_write_reg(r->lowlevel, DS1307_REG_CONTROL, data, 1))
      {
         SERR("[%s] Failed to disable output discretes", __func__);
      }
   }

   SDBG("[%s] Success", __func__);
   return true;
}

/* ----------------------------------------------------------------------------------------------
 * Exported Functions 
 */

bool ds1307_configure(rtci2c_t *ctx, uint8_t i2c_address)
{
   ctx->i2c_address = DS1307_I2C_ADDRESS; /* there is only one */
   ctx->i2c_max_transfer_length = DS1307_I2C_MAX_TRANSFER_LENGTH;
   ctx->i2c_speed = DS1307_I2C_SPEED;
   ctx->devfn_init = ds1307_init;
   ctx->devfn_get_datetime = ds1307_get_datetime;
   ctx->devfn_set_datetime = ds1307_set_datetime;
   return true;
}
