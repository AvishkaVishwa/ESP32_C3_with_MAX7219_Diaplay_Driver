#include "display_manager.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "app_config.h"
#include "max7219.h"

static const char *TAG = "display_manager";
spi_device_handle_t spi;

void display_manager_init(void) {
    ESP_LOGI(TAG, "Initializing display manager");
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 5 * 1000 * 1000, // 5 MHz
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 7,
    };

    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    spi_bus_add_device(SPI2_HOST, &devcfg, &spi);

    max7219_init(spi);
}

void display_manager_show_time(int hour, int minute, int second) {
    max7219_clear(spi);
    max7219_write_digit(spi, 0, second % 10, false);
    max7219_write_digit(spi, 1, second / 10, false);
    max7219_write_digit(spi, 2, minute % 10, false);
    max7219_write_digit(spi, 3, minute / 10, false);
    max7219_write_digit(spi, 4, hour % 10, false);
    max7219_write_digit(spi, 5, hour / 10, false);
}

void display_message(const char* message) {
    max7219_display_text(spi, message);
}

void display_clear(void) {
    max7219_clear(spi);
}