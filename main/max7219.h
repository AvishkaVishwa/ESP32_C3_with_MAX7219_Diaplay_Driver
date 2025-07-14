/*
 * max7219.h
 *
 *  Created on: Jul 9, 2025
 *      Author: GitHub Copilot
 */

#ifndef MAIN_MAX7219_H_
#define MAIN_MAX7219_H_

#include "driver/spi_master.h"
#include <stdint.h>

// MAX7219 registers
#define MAX7219_REG_NOOP         0x00
#define MAX7219_REG_DECODEMODE   0x09
#define MAX7219_REG_INTENSITY    0x0A
#define MAX7219_REG_SCANLIMIT    0x0B
#define MAX7219_REG_SHUTDOWN     0x0C
#define MAX7219_REG_DISPLAYTEST  0x0F

// Digits
#define MAX7219_REG_DIGIT0       0x01
#define MAX7219_REG_DIGIT1       0x02
#define MAX7219_REG_DIGIT2       0x03
#define MAX7219_REG_DIGIT3       0x04
#define MAX7219_REG_DIGIT4       0x05
#define MAX7219_REG_DIGIT5       0x06
#define MAX7219_REG_DIGIT6       0x07
#define MAX7219_REG_DIGIT7       0x08

void max7219_init(spi_device_handle_t spi);
void max7219_send_cmd(spi_device_handle_t spi, uint8_t reg, uint8_t data);
void max7219_clear(spi_device_handle_t spi);
void max7219_set_intensity(spi_device_handle_t spi, uint8_t intensity);
void max7219_write_digit(spi_device_handle_t spi, uint8_t digit, uint8_t value, bool dp);
void max7219_display_text(spi_device_handle_t spi, const char* text);
void max7219_display_number(spi_device_handle_t spi, int32_t number);

#endif /* MAIN_MAX7219_H_ */
