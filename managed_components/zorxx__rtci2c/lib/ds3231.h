/*! \copyright 2024 Zorxx Software. All rights reserved.
 *  \license This file is released under the MIT License. See the LICENSE file for details.
 *  \brief ds3231 real-time clock device interface 
 */
#ifndef _DS3231_H
#define _DS3231_H

#include <stdint.h>
#include <stdbool.h>
#include "rtci2c_private.h"
#include "helpers.h"

/* -----------------------------------------------------------------------------------------------
 * Register definitions 
 * Many of the DS3231 registers are similar to DS1307
 */

#define DS3231_REG_CONTROL          14
   #define DS3231_REG_CONTROL_RS1_BIT   3 /* 0 = 1 Hz       1 = 1.024 kHz */
   #define DS3231_REG_CONTROL_RS2_BIT   4 /* 2 = 4.096 kHz  3 = 8.192 kHz */
   #define DS3231_REG_CONTROL_EOSC_BIT  7 /* oscillator enable (0 to enable) */

#define DS3231_REG_STATUS           15
   #define DS3231_REG_STATUS_A1F_BIT      0 /* alarm flag 1 */
   #define DS3231_REG_STATUS_A2F_BIT      1 /* alarm flag 2 */
   #define DS3231_REG_STATUS_BUSY_BIT     2 /* busy  */
   #define DS3231_REG_STATUS_EN32KHZ_BIT  3 /* enable 32kHz output */
   #define DS3231_REG_STATUS_OSF_BIT      7 /* oscillator stopped */

/* -----------------------------------------------------------------------------------------------
 * Helper Macros
 */

#define DS3231_REG_GET_BIT(data, reg, bit) \
   ((data[DS3231_REG_##reg] & (1 << DS3231_REG_##bit)) != 0)

bool ds3231_configure(rtci2c_t *ctx, uint8_t i2c_address);

#endif /* _DS3231_H */