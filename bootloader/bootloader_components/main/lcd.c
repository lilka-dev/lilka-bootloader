#include "lcd.h"

#include "font5x7.h"
#include "hw.h"

// Rotation 3 in Arduino_GFX's ST7789 driver (MY | MV) with Lilka's 20 px panel offset
#define LCD_MADCTL   0xA0
#define LCD_X_OFFSET 20
#define LCD_Y_OFFSET 0

static void lcd_cmd(uint8_t cmd, const uint8_t *data, int len) {
    spi_select(PIN_LCD_CS, 3);
    lcd_dc(0);
    spi_xfer(cmd);
    lcd_dc(1);
    spi_write(data, len);
    spi_deselect(PIN_LCD_CS);
}

static void lcd_window(int x, int y, int w, int h) {
    x += LCD_X_OFFSET;
    y += LCD_Y_OFFSET;
    uint8_t cols[4] = {x >> 8, x & 0xFF, (x + w - 1) >> 8, (x + w - 1) & 0xFF};
    uint8_t rows[4] = {y >> 8, y & 0xFF, (y + h - 1) >> 8, (y + h - 1) & 0xFF};
    lcd_cmd(0x2A, cols, 4); // CASET
    lcd_cmd(0x2B, rows, 4); // RASET
    spi_select(PIN_LCD_CS, 3);
    lcd_dc(0);
    spi_xfer(0x2C); // RAMWR, pixel data follows until CS goes high
    lcd_dc(1);
}

void lcd_init(void) {
    static const uint8_t colmod = 0x55; // 16 bpp
    static const uint8_t madctl = LCD_MADCTL;

    lcd_cmd(0x01, NULL, 0); // SWRESET
    hw_delay_ms(120);
    lcd_cmd(0x11, NULL, 0); // SLPOUT
    hw_delay_ms(120);
    lcd_cmd(0x3A, &colmod, 1);
    lcd_cmd(0x36, &madctl, 1);
    lcd_cmd(0x21, NULL, 0); // INVON (IPS panel)
    lcd_cmd(0x13, NULL, 0); // NORON
    lcd_fill(0, 0, LCD_W, LCD_H, 0);
    lcd_cmd(0x29, NULL, 0); // DISPON
}

void lcd_fill(int x, int y, int w, int h, uint16_t color) {
    lcd_window(x, y, w, h);
    spi_write_repeat16(color, (uint32_t)w * h);
    spi_deselect(PIN_LCD_CS);
}

void lcd_text(int x, int y, const char *s, uint16_t fg, uint16_t bg) {
    for (; *s && x + LCD_CHAR_W <= LCD_W; s++, x += LCD_CHAR_W) {
        char c = *s;
        if (c < 0x20 || c > 0x7E) c = '?';
        const uint8_t *glyph = font5x7[c - 0x20];

        lcd_window(x, y, LCD_CHAR_W, LCD_CHAR_H);
        for (int row = 0; row < 8; row++) {
            for (int sy = 0; sy < LCD_FONT_SCALE; sy++) {
                for (int col = 0; col < 6; col++) {
                    uint16_t color = (col < 5 && (glyph[col] >> row) & 1) ? fg : bg;
                    spi_write_repeat16(color, LCD_FONT_SCALE);
                }
            }
        }
        spi_deselect(PIN_LCD_CS);
    }
}
