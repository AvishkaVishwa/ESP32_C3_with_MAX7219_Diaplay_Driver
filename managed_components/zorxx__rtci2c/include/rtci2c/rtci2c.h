/*! \copyright 2024 Zorxx Software. All rights reserved.
 *  \license This file is released under the MIT License. See the LICENSE file for details.
 *  \brief RTC library interface
 */
#ifndef _RTCI2C_H
#define _RTCI2C_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#if defined(__linux__)
   #include "rtci2c/sys_linux.h"
#elif defined(ESP_PLATFORM)
   #include "rtci2c/sys_esp.h"
#else
   #error "Supported OS type not detected"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void *rtci2c_context;

typedef enum
{
   RTCI2C_DEVICE_DS1307,
   RTCI2C_DEVICE_DS3231

   /* The following are coming soon
   RTCI2C_DEVICE_DS1302,
   RTCI2C_DEVICE_PCF8563, */
} rtci2c_device_type;

rtci2c_context rtci2c_init(rtci2c_device_type device, uint8_t i2c_address, i2c_lowlevel_config *config);
bool rtci2c_deinit(rtci2c_context context);
bool rtci2c_get_datetime(rtci2c_context context, struct tm *datetime);
bool rtci2c_set_datetime(rtci2c_context context, struct tm *datetime);

#ifdef __cplusplus
}
#endif

#endif /* _RTCI2C_H */
