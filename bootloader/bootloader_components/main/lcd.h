// ST7789 280x240 (landscape, same orientation as Keira) with a scaled 5x7 font.
#pragma once

#include <stdint.h>

#define LCD_W 280
#define LCD_H 240

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define LCD_FONT_SCALE 2
#define LCD_CHAR_W     (6 * LCD_FONT_SCALE)
#define LCD_CHAR_H     (8 * LCD_FONT_SCALE)

void lcd_init(void);
void lcd_fill(int x, int y, int w, int h, uint16_t color);
void lcd_text(int x, int y, const char *s, uint16_t fg, uint16_t bg);
