/*! \copyright 2024 Zorxx Software. All rights reserved.
 *  \license This file is released under the MIT License. See the LICENSE file for details.
 *  \brief RTC library
 */
#include <string.h> /* memset */
#include <stdlib.h> /* malloc */
#include <inttypes.h>
#include "helpers.h"
#include "ds1307.h"
#include "ds3231.h"
#include "rtci2c_private.h"

const char *RTCI2C_DAY_OF_WEEK[] = \
   { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

/* ----------------------------------------------------------------------------------------------
 * Exported Functions
 */

rtci2c_context rtci2c_init(rtci2c_device_type device, uint8_t i2c_address, i2c_lowlevel_config *config)
{
   rtci2c_t *r = (rtci2c_t *) malloc(sizeof(*r));
   bool configured = false;
   if(NULL == r)
      return NULL;
   memset(r, 0, sizeof(*r));

   switch(device)
   {
      case RTCI2C_DEVICE_DS1307:
         configured = ds1307_configure(r, i2c_address);
         break;
      case RTCI2C_DEVICE_DS3231:
         configured = ds3231_configure(r, i2c_address);
         break;
      default:
         SERR("[%s] Unsupported device type (%d)", __func__, device);
         break;
   }

   if(!configured)
   {
      SERR("[%s] Device-specific configuration failed", __func__);
      free(r);
      return NULL;
   }

   SDBG("[%s] Using i2c address 0x%02x @ %" PRIu32 " hz", __func__, r->i2c_address, r->i2c_speed);
   r->lowlevel = i2c_ll_init(r->i2c_address, r->i2c_speed, r->i2c_timeout, config);
   if(NULL == r->lowlevel)
   {
      SERR("[%s] Low-level interface configuration failed", __func__);
      free(r);
      return NULL;
   }

   if(NULL != r->devfn_init)
   {
      if(!r->devfn_init(r))
      {
         SERR("[%s] Device-specific initialization failed", __func__);
         free(r);
         return NULL;
      }
   }

   SDBG("[%s] Initialized", __func__);
   return (rtci2c_context) r;
}

bool rtci2c_deinit(rtci2c_context context)
{
   rtci2c_t *r = (rtci2c_t *) context;
   if(NULL != r->devfn_deinit)
      r->devfn_deinit(r);
   free(r);
   return true;
}

bool rtci2c_get_datetime(rtci2c_context context, struct tm *datetime)
{
   rtci2c_t *r = (rtci2c_t *) context;
   if(NULL == r->devfn_get_datetime)
      return false;
   return r->devfn_get_datetime(r, datetime);
}

bool rtci2c_set_datetime(rtci2c_context context, struct tm *datetime)
{
   rtci2c_t *r = (rtci2c_t *) context;
   if(NULL == r->devfn_set_datetime)
      return false;
   return r->devfn_set_datetime(r, datetime);
}