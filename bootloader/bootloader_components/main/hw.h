// Bare-metal Lilka v2 hardware access for the 2nd stage bootloader:
// bit-banged SPI on the shared display/SD bus and button GPIOs.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Pins (see lilka SDK config.h, LILKA_VERSION == 2)
#define PIN_SPI_SCK  18
#define PIN_SPI_MOSI 17
#define PIN_SPI_MISO 8
#define PIN_LCD_CS   7
#define PIN_LCD_DC   15
#define PIN_SD_CS    16
#define PIN_SLEEP    46 // HIGH = peripherals powered

#define PIN_BTN_UP    38
#define PIN_BTN_DOWN  41
#define PIN_BTN_LEFT  39
#define PIN_BTN_RIGHT 40
#define PIN_BTN_START 4
#define PIN_BTN_A     5
#define PIN_BTN_B     6

// Work memory for the menu, SD and flashing (at most BOOT_SCRATCH_SIZE bytes).
// Kept out of .bss on purpose: the S3 ROM loader fails ("ets_loader.c 78") when the bootloader's
// DRAM segment grows past what the stock bootloader uses. This internal SRAM isn't part of the
// bootloader image and is only used before the app is loaded (which may then overwrite it).
#define BOOT_SCRATCH_ADDR 0x3FCC0000
#define BOOT_SCRATCH_SIZE 0x8000
#define SD_SECTOR_BUF     ((uint8_t *)(BOOT_SCRATCH_ADDR + BOOT_SCRATCH_SIZE - 512))

void hw_init(void);
bool hw_btn(int pin); // true while pressed (active low)
void hw_delay_ms(uint32_t ms);

// SPI: mode 0 (SD) or mode 3 (ST7789). `slow` adds delays for SD init (<400 kHz).
void spi_select(int cs_pin, int mode);
void spi_deselect(int cs_pin);
void spi_set_slow(bool slow);
uint8_t spi_xfer(uint8_t out);
void spi_write(const uint8_t *data, size_t len);
void spi_write_repeat16(uint16_t value, uint32_t count);
void spi_read(uint8_t *data, size_t len);
void lcd_dc(int level);
