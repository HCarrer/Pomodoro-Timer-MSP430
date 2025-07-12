#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <msp430.h>
#include <stdint.h>

#define PCF8574_ADDR (0x27) // User-specified I2C address

#define LCD_RS_BIT (1 << 0) // Register Select (0 for command, 1 for data)
#define LCD_RW_BIT (1 << 1) // Read/Write (0 for write, 1 for read - kept 0 for this library)
#define LCD_EN_BIT (1 << 2) // Enable
#define LCD_BL_BIT (1 << 3) // Backlight Control (1 for ON, 0 for OFF)
#define LCD_D4_BIT (1 << 4) // Data Bit 4
#define LCD_D5_BIT (1 << 5) // Data Bit 5
#define LCD_D6_BIT (1 << 6) // Data Bit 6
#define LCD_D7_BIT (1 << 7) // Data Bit 7

#define LCD_CLEAR_DISPLAY           0x01
#define LCD_RETURN_HOME             0x02
#define LCD_ENTRY_MODE_SET          0x04
#define LCD_DISPLAY_CONTROL         0x08
#define LCD_CURSOR_SHIFT            0x10
#define LCD_FUNCTION_SET            0x20
#define LCD_SET_CGRAM_ADDR          0x40
#define LCD_SET_DDRAM_ADDR          0x80

#define LCD_8BIT_MODE               0x10 // Not used in this 4-bit library
#define LCD_4BIT_MODE               0x00 // Base for 4-bit mode selection
#define LCD_2LINE                   0x08 // For 2-line display
#define LCD_1LINE                   0x00 // For 1-line display
#define LCD_5x10DOTS                0x04 // 5x10 dot characters
#define LCD_5x8DOTS                 0x00 // 5x8 dot characters

#define LCD_FUNCTION_SET_4BIT_2LINE_5x8DOTS (LCD_FUNCTION_SET | LCD_4BIT_MODE | LCD_2LINE | LCD_5x8DOTS) // Should be 0x28
#define LCD_DISPLAY_ON_CURSOR_OFF   (LCD_DISPLAY_CONTROL | 0x04) // D=1, C=0, B=0 -> 0x0C
#define LCD_DISPLAY_ON_CURSOR_ON    (LCD_DISPLAY_CONTROL | 0x04 | 0x02) // D=1, C=1, B=0 -> 0x0E
#define LCD_DISPLAY_ON_CURSOR_BLINK (LCD_DISPLAY_CONTROL | 0x04 | 0x02 | 0x01) // D=1, C=1, B=1 -> 0x0F
#define LCD_ENTRY_MODE_INC_NO_SHIFT (LCD_ENTRY_MODE_SET | 0x02) // Increment cursor, no display shift -> 0x06

#define LCD_DISPLAY_ON_CURSOR_ON_BLINK_ON    0x0F
#define LCD_DISPLAY_ON_CURSOR_ON_BLINK_OFF   0x0E
#define LCD_DISPLAY_ON_CURSOR_OFF_BLINK_OFF  0x0C

void configure_lcd(void);
void lcd_send_command(uint8_t command);
void lcd_send_data(uint8_t data);
void lcd_print_char(char character);
void print_message(const char* str);
void position_lcd_cursor(uint8_t row, uint8_t col);
void clear_lcd_screen(void);
void blink_cursor(void);
void stop_blinking_cursor(void);

#endif
