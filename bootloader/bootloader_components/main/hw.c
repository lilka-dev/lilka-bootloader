#include "hw.h"

#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "soc/gpio_periph.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"
#include "soc/soc.h"

static int spi_mode;
static bool spi_slow;

#define BIT_LO(pin) (1UL << (pin))
#define BIT_HI(pin) (1UL << ((pin) - 32))

static inline void pin_set(int pin) {
    if (pin < 32) {
        REG_WRITE(GPIO_OUT_W1TS_REG, BIT_LO(pin));
    } else {
        REG_WRITE(GPIO_OUT1_W1TS_REG, BIT_HI(pin));
    }
}

static inline void pin_clr(int pin) {
    if (pin < 32) {
        REG_WRITE(GPIO_OUT_W1TC_REG, BIT_LO(pin));
    } else {
        REG_WRITE(GPIO_OUT1_W1TC_REG, BIT_HI(pin));
    }
}

static inline int pin_get(int pin) {
    if (pin < 32) {
        return (REG_READ(GPIO_IN_REG) >> pin) & 1;
    }
    return (REG_READ(GPIO_IN1_REG) >> (pin - 32)) & 1;
}

static void pin_output(int pin, int level) {
    esp_rom_gpio_pad_select_gpio(pin);
    if (level) {
        pin_set(pin);
    } else {
        pin_clr(pin);
    }
    esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
    if (pin < 32) {
        REG_WRITE(GPIO_ENABLE_W1TS_REG, BIT_LO(pin));
    } else {
        REG_WRITE(GPIO_ENABLE1_W1TS_REG, BIT_HI(pin));
    }
}

static void pin_input(int pin, bool pullup) {
    esp_rom_gpio_pad_select_gpio(pin);
    if (pin < 32) {
        REG_WRITE(GPIO_ENABLE_W1TC_REG, BIT_LO(pin));
    } else {
        REG_WRITE(GPIO_ENABLE1_W1TC_REG, BIT_HI(pin));
    }
    PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[pin]);
    if (pullup) {
        REG_SET_BIT(GPIO_PIN_MUX_REG[pin], FUN_PU);
    }
}

void hw_init(void) {
    pin_output(PIN_SLEEP, 1);
    pin_output(PIN_LCD_CS, 1);
    pin_output(PIN_SD_CS, 1);
    pin_output(PIN_LCD_DC, 1);
    pin_output(PIN_SPI_SCK, 0);
    pin_output(PIN_SPI_MOSI, 1);
    pin_input(PIN_SPI_MISO, true);

    static const int buttons[] = {
        PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_BTN_START, PIN_BTN_A, PIN_BTN_B,
    };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        pin_input(buttons[i], true);
    }
    esp_rom_delay_us(1000); // let pull-ups settle before the first read
}

bool hw_btn(int pin) {
    return pin_get(pin) == 0;
}

void hw_delay_ms(uint32_t ms) {
    while (ms--) {
        esp_rom_delay_us(1000);
    }
}

void spi_select(int cs_pin, int mode) {
    spi_mode = mode;
    if (mode == 3) {
        pin_set(PIN_SPI_SCK);
    } else {
        pin_clr(PIN_SPI_SCK);
    }
    pin_clr(cs_pin);
}

void spi_deselect(int cs_pin) {
    pin_set(cs_pin);
}

void spi_set_slow(bool slow) {
    spi_slow = slow;
}

uint8_t spi_xfer(uint8_t out) {
    uint8_t in = 0;
    for (int i = 7; i >= 0; i--) {
        if (spi_mode == 3) {
            pin_clr(PIN_SPI_SCK);
        }
        if (out & (1 << i)) {
            pin_set(PIN_SPI_MOSI);
        } else {
            pin_clr(PIN_SPI_MOSI);
        }
        if (spi_slow) {
            esp_rom_delay_us(2);
        }
        pin_set(PIN_SPI_SCK);
        in = (in << 1) | pin_get(PIN_SPI_MISO);
        if (spi_slow) {
            esp_rom_delay_us(2);
        }
        if (spi_mode == 0) {
            pin_clr(PIN_SPI_SCK);
        }
    }
    return in;
}

void spi_write(const uint8_t *data, size_t len) {
    while (len--) {
        spi_xfer(*data++);
    }
}

void spi_write_repeat16(uint16_t value, uint32_t count) {
    while (count--) {
        spi_xfer(value >> 8);
        spi_xfer(value & 0xFF);
    }
}

void spi_read(uint8_t *data, size_t len) {
    while (len--) {
        *data++ = spi_xfer(0xFF);
    }
}

void lcd_dc(int level) {
    if (level) {
        pin_set(PIN_LCD_DC);
    } else {
        pin_clr(PIN_LCD_DC);
    }
}
