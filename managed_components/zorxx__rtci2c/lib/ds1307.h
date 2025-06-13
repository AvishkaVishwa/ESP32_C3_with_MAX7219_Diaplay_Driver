/*! \copyright 2024 Zorxx Software. All rights reserved.
 *  \license This file is released under the MIT License. See the LICENSE file for details.
 *  \brief ds1307 real-time clock device interface 
 */
#ifndef _DS1307_H
#define _DS1307_H

#include <stdint.h>
#include <stdbool.h>
#include "rtci2c_private.h"
#include "helpers.h"

#define DS1307_I2C_ADDRESS             0x68
#define DS1307_I2C_SPEED               400000 /* hz */
#define DS1307_I2C_MAX_TRANSFER_LENGTH 64 /* bytes */
#define DS1307_I2C_TIMEOUT             20 /* milliseconds */

/* -----------------------------------------------------------------------------------------------
 * Register definitions 
 */

#define DS1307_REG_SECONDS          0
   #define DS1307_REG_MASK_SECONDS    0x7f
   #define DS1307_REG_SECONDS_CS_BIT   7

#define DS1307_REG_MINUTES          1
   #define DS1307_REG_MASK_MINUTES    0x7f

#define DS1307_REG_HOURS            2
#define DS1307_REG_HOURS_12         2
#define DS1307_REG_HOURS_24         2
   #define DS1307_REG_MASK_HOURS_24   0x3f
   #define DS1307_REG_MASK_HOURS_12   0x1f
   #define DS1307_REG_HOURS_AMPM_BIT   5 /* 1 = PM, 0 = AM */
   #define DS1307_REG_HOURS_24_BIT     6 /* 1 = 12 hour, 0 = 24 hour*/

#define DS1307_REG_DAYOFWEEK        3
   #define DS1307_REG_MASK_DAYOFWEEK  0x07

#define DS1307_REG_DAYOFMONTH       4
   #define DS1307_REG_MASK_DAYOFMONTH 0x3f

#define DS1307_REG_MONTH            5
   #define DS1307_REG_MASK_MONTH      0x1f

#define DS1307_REG_YEAR             6
   #define DS1307_REG_MASK_YEAR       0xff

#define DS1307_REG_CONTROL          7
   #define DS1307_REG_CONTROL_RS0_BIT  0 /* 0 = 1 Hz       1 = 4.096 kHz */
   #define DS1307_REG_CONTROL_RS1_BIT  1 /* 2 = 8.192 kHz  3 = 32.768 kHz */
   #define DS1307_REG_CONTROL_SQWE_BIT 4 /* square wave enable */
   #define DS1307_REG_CONTROL_OUT_BIT  7 /* output enable */

#define DS1307_REG_RAM_START        8
#define DS1307_REG_RAM_END          63  /* total of 56 registers */
#define DS1307_REG_RAM_COUNT        (DS1307_REG_RAM_END - DS1307_REG_RAM_START + 1)

/* -----------------------------------------------------------------------------------------------
 * Helper Macros
 */

#define DS1307_REG_GET(data, field) \
   RTC_BCD_TO_DEC(data[DS1307_REG_##field] & DS1307_REG_MASK_##field)
#define DS1307_REG_GET_BIT(data, reg, bit) \
   ((data[DS1307_REG_##reg] & (1 << DS1307_REG_##bit)) != 0)

bool ds1307_configure(rtci2c_t *ctx, uint8_t i2c_address);
bool ds1307_get_datetime(void *rtci2c_ctx, struct tm *datetime);
bool ds1307_set_datetime(void *rtci2c_ctx, struct tm *datetime);

#endif /* _DS1307_H */