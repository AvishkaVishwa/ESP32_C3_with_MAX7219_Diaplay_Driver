/*! \copyright 2024 Zorxx Software. All rights reserved.
 *  \license This file is released under the MIT License. See the LICENSE file for details.
 *  \brief rtci2c Library internal interface
 */
#ifndef _RTCI2C_PRIVATE_H
#define _RTCI2C_PRIVATE_H

#include <stdbool.h>
#include <stdint.h>
#include "rtci2c/rtci2c.h"
#include "sys.h" /* include after rtci2c.h */

typedef bool (*pfn_rtcdevice_init)(void *rtci2c_ctx);
typedef bool (*pfn_rtcdevice_datetime)(void *rtci2c_ctx, struct tm *datetime);

typedef struct rtci2c_s
{
    uint8_t i2c_address;
    uint8_t i2c_max_transfer_length;
    uint8_t i2c_timeout;
    uint32_t i2c_speed;

    pfn_rtcdevice_init devfn_init;
    pfn_rtcdevice_init devfn_deinit;
    pfn_rtcdevice_datetime devfn_get_datetime;
    pfn_rtcdevice_datetime devfn_set_datetime;

    void *lowlevel;
} rtci2c_t;

#endif /* _RTCI2C_PRIVATE_H */
