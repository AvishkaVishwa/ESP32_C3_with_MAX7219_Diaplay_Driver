#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <time.h>

void sync_time(void);
void update_time(void);
void time_utils_init_sntp(void);
void time_utils_obtain_time(void);
void time_utils_set_system_time(const char* tzid);
void time_utils_set_time_from_string(const char* time_str);

#endif // TIME_UTILS_H
