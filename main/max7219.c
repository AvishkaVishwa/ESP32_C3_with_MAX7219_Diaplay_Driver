#include "max7219.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdint.h>

//static const char *TAG = "MAX7219";

void max7219_send_cmd(spi_device_handle_t spi, uint8_t reg, uint8_t data) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    uint16_t cmd = (reg << 8) | data;
    t.length = 16;
    t.tx_buffer = &cmd;
    esp_err_t ret = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
}

void max7219_init(spi_device_handle_t spi) {
    max7219_send_cmd(spi, MAX7219_REG_SHUTDOWN, 1);
    max7219_send_cmd(spi, MAX7219_REG_DECODEMODE, 0x00);
    max7219_send_cmd(spi, MAX7219_REG_SCANLIMIT, 7);
    max7219_send_cmd(spi, MAX7219_REG_INTENSITY, 1);
    max7219_send_cmd(spi, MAX7219_REG_DISPLAYTEST, 0);
    max7219_clear(spi);
}

void max7219_clear(spi_device_handle_t spi) {
    for (int i = 1; i <= 8; i++) {
        max7219_send_cmd(spi, i, 0x00);
    }
}

void max7219_set_intensity(spi_device_handle_t spi, uint8_t intensity) {
    if (intensity > 15) {
        intensity = 15;
    }
    max7219_send_cmd(spi, MAX7219_REG_INTENSITY, intensity);
}

// This is a basic font, supporting 0-9, A-F, and some symbols.
// This can be expanded.
static const uint8_t font[] = {
    0x7E, // 0
    0x30, // 1
    0x6D, // 2
    0x79, // 3
    0x33, // 4
    0x5B, // 5
    0x5F, // 6
    0x70, // 7
    0x7F, // 8
    0x7B, // 9
    0x01, // .
};

void max7219_display_text(spi_device_handle_t spi, const char* text) {
    int len = strlen(text);
    for (int i = 0; i < 8; i++) {
        if (i < len) {
            char c = text[len - 1 - i];
            uint8_t val = 0;
            if (c >= '0' && c <= '9') {
                val = font[c - '0'];
            } else if (c == '.') {
                // find previous digit and add the dot
                if (i > 0) {
                    char prev_c = text[len - 1 - (i-1)];
                     if (prev_c >= '0' && prev_c <= '9') {
                        uint8_t prev_val = font[prev_c - '0'];
                        max7219_send_cmd(spi, MAX7219_REG_DIGIT0 + (i-1), prev_val | 0x80);
                     }
                }
                 max7219_send_cmd(spi, MAX7219_REG_DIGIT0 + i, 0x00); // clear current digit
                 continue;
            }
            max7219_send_cmd(spi, MAX7219_REG_DIGIT0 + i, val);
        } else {
            max7219_send_cmd(spi, MAX7219_REG_DIGIT0 + i, 0x00); // Clear remaining digits
        }
    }
}


void max7219_display_number(spi_device_handle_t spi, int32_t number) {
    char buf[9];
    snprintf(buf, sizeof(buf), "%ld", number);
    max7219_display_text(spi, buf);
}

void max7219_write_digit(spi_device_handle_t spi, uint8_t digit, uint8_t value, bool dp) {
    if (digit > 7 || value > 15) return;
    uint8_t val = font[value];
    if (dp) {
        val |= 0x80;
    }
    max7219_send_cmd(spi, MAX7219_REG_DIGIT0 + digit, val);
}
