#include "lcd_display.h"

static uint8_t backlight_state = LCD_BL_BIT; // Default to backlight ON

static void i2c_init_msp430(void) {
    P3SEL |= BIT0 | BIT1;                     // Assign P3.0 to UCB0SDA and P3.1 to UCB0SCL
    UCB0CTL1 |= UCSWRST;                      // Enable SW reset
    UCB0CTL0 = UCMST + UCMODE_3 + UCSYNC;     // I2C Master, synchronous mode
    UCB0CTL1 = UCSSEL_2 + UCSWRST;            // Use SMCLK, keep SW reset
    UCB0BR0 = 12;                             // fSCL = SMCLK/12 = ~87kHz with SMCLK ~1.045MHz
    UCB0BR1 = 0;
    UCB0I2CSA = PCF8574_ADDR;                 // Set slave address
    UCB0CTL1 &= ~UCSWRST;                     // Clear SW reset, resume operation
}

static void i2c_write_byte(uint8_t slave_addr, uint8_t data) {
    (void)slave_addr; // slave_addr is already set in UCB0I2CSA during init
                      // If multiple slaves were on the bus, UCB0I2CSA would be updated here.

    while (UCB0CTL1 & UCTXSTP);             // Ensure stop condition got sent
    UCB0CTL1 |= UCTR + UCTXSTT;             // I2C TX, start condition
    while (!(UCB0IFG & UCTXIFG) && (UCB0CTL1 & UCTXSTT)); // Corrected UCTXIFG0 to UCTXIFG
    if (UCB0IFG & UCNACKIFG) {              // Check for NACK
        UCB0CTL1 |= UCTXSTP;                // Send STOP if NACK received
        UCB0IFG &= ~UCNACKIFG;              // Clear NACK flag
        return;                             // Exit on NACK
    }
    UCB0TXBUF = data;                       // Load data into buffer
    while (!(UCB0IFG & UCTXIFG));           // Wait for TX to complete (UCBxTXBUF is empty again) [2] Corrected UCTXIFG0 to UCTXIFG
    UCB0CTL1 |= UCTXSTP;                    // I2C stop condition
    while(UCB0CTL1 & UCTXSTP);              // Ensure stop condition got sent
}

static void lcd_write_pcf8574(uint8_t pcf_byte) {
    i2c_write_byte(PCF8574_ADDR, pcf_byte);
    __delay_cycles(50); // Small delay for PCF8574 to process
}

static void lcd_pulse_enable(uint8_t data_with_rs_bl_and_data) {
    lcd_write_pcf8574(data_with_rs_bl_and_data | LCD_EN_BIT); // E = 1 (high)
    __delay_cycles(50); // Enable pulse width (min 450ns for HD44780)
                       // At 1MHz, 50 cycles = 50us, which is ample.
    lcd_write_pcf8574(data_with_rs_bl_and_data & ~LCD_EN_BIT); // E = 0 (low)
    __delay_cycles(100); // Execution time for most commands (min 37us)
                        // Using 100us provides a safe margin.
}

static void lcd_write_nibble(uint8_t nibble, uint8_t rs_mode) {
    uint8_t pcf_data;
    pcf_data = (nibble << 4) & (LCD_D4_BIT | LCD_D5_BIT | LCD_D6_BIT | LCD_D7_BIT);
    if (rs_mode) {
        pcf_data |= LCD_RS_BIT; // Set RS bit for data
    }
    pcf_data |= backlight_state;

    lcd_pulse_enable(pcf_data);
}

void configure_lcd(void) {
    i2c_init_msp430();
    __delay_cycles(50000); // Wait >40ms after VCC rises to 2.7V (HD44780 spec)
                           // Using 50ms at 1MHz for safety.

    lcd_write_nibble(0x03, 0); // RS=0
    __delay_cycles(5000);  // Wait >4.1ms

    lcd_write_nibble(0x03, 0);
    __delay_cycles(200);   // Wait >100us

    lcd_write_nibble(0x03, 0);
    __delay_cycles(200);   // Wait >100us (datasheet just says "wait")

    lcd_write_nibble(0x02, 0);
    __delay_cycles(200);   // Wait (min 37us, using more for safety)

    lcd_send_command(LCD_FUNCTION_SET_4BIT_2LINE_5x8DOTS); // 0x28: 2 lines, 5x8 font
    lcd_send_command(LCD_DISPLAY_ON_CURSOR_OFF);          // 0x0C: Display ON, Cursor OFF, Blink OFF
    clear_lcd_screen();                                          // 0x01: Clear display
    lcd_send_command(LCD_ENTRY_MODE_INC_NO_SHIFT);        // 0x06: Increment cursor, no display shift

    backlight_state = LCD_BL_BIT; // Store state for subsequent writes
    uint8_t current_pcf_val_for_backlight_only = backlight_state; // RS=0, E=0, Data=0, R/W=0 (implicitly)
    lcd_write_pcf8574(current_pcf_val_for_backlight_only); // Update backlight immediately
}

void lcd_send_command(uint8_t command) {
    lcd_write_nibble(command >> 4, 0);   // Send high nibble, RS=0
    lcd_write_nibble(command & 0x0F, 0); // Send low nibble, RS=0
    if (command == LCD_CLEAR_DISPLAY || command == LCD_RETURN_HOME) { // Corrected logical OR
        __delay_cycles(2000); // These commands need >1.52ms [3]
                              // 2000 cycles at 1MHz = 2ms
    }
}

void lcd_send_data(uint8_t data) {
    lcd_write_nibble(data >> 4, 1);   // Send high nibble, RS=1 (data)
    lcd_write_nibble(data & 0x0F, 1); // Send low nibble, RS=1 (data)
}

void lcd_print_char(char character) {
    lcd_send_data((uint8_t)character);
}

void print_message(const char* str) {
    while (*str) {
        lcd_print_char(*str++);
    }
}

void position_lcd_cursor(uint8_t row, uint8_t col) {
    uint8_t ddram_addr;
    switch (row) {
        case 0: ddram_addr = LCD_SET_DDRAM_ADDR + col; break;       // Line 0
        case 1: ddram_addr = LCD_SET_DDRAM_ADDR + 0x40 + col; break; // Line 1 for 16x2
        default: ddram_addr = LCD_SET_DDRAM_ADDR + 0x40 + col; break; // Line 1 for 16x2
    }
    lcd_send_command(ddram_addr);
}

void clear_lcd_screen(void) {
    lcd_send_command(LCD_CLEAR_DISPLAY);
}

void blink_cursor(void) {
    lcd_send_command(0x0F);
}

void stop_blinking_cursor(void) {
    lcd_send_command(0x0C);
}
