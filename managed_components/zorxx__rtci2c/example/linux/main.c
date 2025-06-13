/*! \copyright 2024 Zorxx Software. All rights reserved.
 *  \license This file is released under the MIT License. See the LICENSE file for details.
 *  \brief rtci2c library Linux example application
 */
#include <stdio.h>
#include "rtci2c/rtci2c.h"

#define MSG(...) fprintf(stderr, __VA_ARGS__)

#define DEVICE_I2C_ADDRESS   0 /* let the library figure it out */
#define I2C_BUS              "/dev/i2c-0"

int main(int argc, char *argv[])
{
   rtci2c_context ctx;
   i2c_lowlevel_config config = { I2C_BUS };

   ctx = rtci2c_init(RTCI2C_DEVICE_DS1307, DEVICE_I2C_ADDRESS, &config);
   if(NULL == ctx)
   {
      MSG("[rtci2c] Initialization failed\n");
   }
   else
   {
      struct tm datetime;
      if(!rtci2c_get_datetime(ctx, &datetime))
      {
         MSG("[rtci2c] Date/time query failed\n");
      }
      else
      {
         char message[128];
         strftime(message, sizeof(message), "%a %b %d %H:%M:%S", &datetime);
         MSG("[rtci2c] %s\n", message);
      }
      rtci2c_deinit(ctx);
   }

   MSG("[rtci2c] Test application finished\n");

   return 0;
}
