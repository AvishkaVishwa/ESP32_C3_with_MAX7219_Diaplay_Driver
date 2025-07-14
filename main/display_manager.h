#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <stdbool.h>
#include "driver/spi_master.h"

extern bool display_initialized;

// Function prototypes for display management
void display_manager_init(void);
void display_manager_show_time(int hour, int minute, int second);
void display_message(const char* message);
void display_clear(void);
void test_display(void);

#endif // DISPLAY_MANAGER_H
