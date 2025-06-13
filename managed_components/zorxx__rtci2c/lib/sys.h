/*! \copyright 2024 Zorxx Software. All rights reserved.
 *  \license This file is released under the MIT License. See the LICENSE file for details.
 *  \brief lowlevel interface
 */
#ifdef _SYS_PORTABILITY_H
   #ifndef SYS_PORTABILITY_VERSION
      #define SYS_PORTABILITY_VERSION 1
   #else
      #if SYS_PORTABILITY_VERSION != 1
         #error "System portability version mismatch"
      #endif
   #endif
#else
#define _SYS_PORTABILITY_H

#include <stdbool.h>
#include <stdint.h>
#if defined(__linux__)
   #include "sys_linux.h"
#elif defined(ESP_PLATFORM)
   #include "sys_esp.h"
#else
   #error "Supported OS type not detected"
#endif
  
/* Make the definitions of each sys function weak to allow multiple libraries to define
 * the functionality, but the linker will choose only one. This is safe since there is
 * a version check at the top of this file to ensure all instances of the system portability
 * layer are the same. */
#define SYS_WEAK __attribute__((weak))

/* i2c */
typedef void *i2c_lowlevel_context;
i2c_lowlevel_context i2c_ll_init(uint8_t i2c_address, uint32_t i2c_speed, uint32_t i2c_timeout_ms,
                                 i2c_lowlevel_config *config);
bool i2c_ll_deinit(i2c_lowlevel_context ctx);
bool i2c_ll_write(i2c_lowlevel_context ctx, uint8_t *data, uint8_t length);
bool i2c_ll_write_reg(i2c_lowlevel_context ctx, uint8_t reg, uint8_t *data, uint8_t length);
bool i2c_ll_read(i2c_lowlevel_context ctx, uint8_t *data, uint8_t length);
bool i2c_ll_read_reg(i2c_lowlevel_context ctx, uint8_t reg, uint8_t *data, uint8_t length);

/* time */
#if defined(ESP_PLATFORM)
   #include "rom/ets_sys.h"  /* ets_delay_us */
   __inline int sys_delay_us(size_t x) { ets_delay_us(x); return 0; }
#elif defined(__linux__)
   #define sys_delay_us(x) usleep(x)
#endif
uint64_t sys_microsecond_tick(void);

/* mutex */
typedef void *mutex_lowlevel;
mutex_lowlevel sys_mutex_init(void);
bool sys_mutex_deinit(mutex_lowlevel mutex);
bool sys_mutex_lock(mutex_lowlevel mutex);
bool sys_mutex_unlock(mutex_lowlevel mutex);

#endif /* _SYS_PORTABILITY_H */
