// Conexões:
// Receptor IR: GND -> GND / VCC -> 3.3V / Signal -> P2.0
// Display LCD: GND -> GND / VCC -> 5V / SDA -> P3.0 / SCL -> P3.1

// Como funciona:
// Tela Inicial: aguarda o clique do botão OK no controle para avançar
// Telas de Configuração do Tempo:
//      < e > controlam se vai alterar a dezena ou a unidade dos minutos
//      ^ e V subtraem ou somam em 1 a minutagem atual. 0 -> 1 -> 2 -> ... -> 99 -> 0 -> 1
//      números controlam individualmente a dezena ou a unidade

// TODO: reset nos botoes da placa


#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <msp430.h>

#include "lcd_display.h" 

#define PULSE_ZERO_TICKS 1700
#define PULSE_ONE_TICKS  3000

#define WELCOME_STEP 0
#define FOCUS_TIME_SET_STEP 1
#define RESTING_TIME_COUNTER_STEP 2
#define TIMER_STEP 3

#define GREEN_LED 0
#define RED_LED 1

#define MINUTES_TENTH 1
#define MINUTES_UNIT 2

#define BUZZER_BEEP_DURATION 3

const char hexTable[] = "0123456789ABCDEF";
volatile char irPulseBits[32];
char irPulseBitsAddr[8];
char irPulseBitsAddrHex[3];
char irPulseBitsCmd[8];
char irPulseBitsCmdHex[3];
char irPulseBitsAddrInv[8];
char irPulseBitsAddrInvHex[3];
char irPulseBitsCmdInv[8];
char irPulseBitsCmdInvHex[3];

volatile unsigned int irBitCount = 0;
volatile int signalReady = 0;
volatile int currentStep = WELCOME_STEP;

char focus_minutes_tenth = '0';
char focus_minutes_unit = '1';

char resting_minutes_tenth = '0';
char resting_minutes_unit = '1';

char* timer_minutes = "01";
char* timer_seconds = "00";

volatile int timer_minutes_int = 0;
volatile int timer_seconds_int = 0;
volatile int timer_active = 0;
volatile int current_timer_type = FOCUS_TIME_SET_STEP;

int isEditing = MINUTES_TENTH; // 1 = tenth, 2 = unit

unsigned int i;

void configure_receiver(void);
void configure_countdown_timer(void);
void configure_msp_button(void);
void configure_buzzer(void);
void process_signal();
bool get_value(char* button);
void handle_welcome_step();
void handle_focus_time_set_step();
void handle_rest_time_set_step();
void byteToHex(const char input[8], char hex[2]);
void show_focus_display();
void show_rest_display();
void show_counter_display();
void increment_minutes();
void decrement_minutes();
void decrement_timer(int step);
void reset();

void main(void) {
    WDTCTL = WDTPW | WDTHOLD; // Stop watchdog timer

    configure_msp_button();
    configure_receiver();
    configure_buzzer();

    lcd_init(); // Initialize the LCD (includes I2C initialization)
    lcd_backlight(1); // Turn backlight ON
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print_string("OK para escolher"); 
    lcd_set_cursor(1, 0);
    lcd_print_string("o tempo de foco");  

    __enable_interrupt();   // Enable global interrupts (GIE)

    while (1) {
        if (currentStep <= RESTING_TIME_COUNTER_STEP) {
            if (signalReady) {
                process_signal();
                
                int previousStep = currentStep; // Remember the step before processing
                
                if (currentStep == WELCOME_STEP) {
                    handle_welcome_step();
                } else if (currentStep == FOCUS_TIME_SET_STEP) {
                    handle_focus_time_set_step();
                } else if (currentStep == RESTING_TIME_COUNTER_STEP) {
                    handle_rest_time_set_step();
                } 
                
                // If step changed, immediately show the new step's display
                if (currentStep != previousStep) {
                    if (currentStep == FOCUS_TIME_SET_STEP) {
                        // Show focus step display immediately
                        show_focus_display();
                    } else if (currentStep == RESTING_TIME_COUNTER_STEP) {
                        // Show resting step display immediately  
                        show_rest_display();
                    }
                }
                
                lcd_backlight(1);
                signalReady = 0;
                irBitCount = 0;
                TA1CCTL1 &= ~CCIFG;
                TA1CCTL1 |= CCIE;
            }
        } else {
            if (currentStep == TIMER_STEP) {
                // Start with focus timer if not already started
                if (!timer_active) {
                    start_timer(FOCUS_TIME_SET_STEP);
                }
                
                // Timer updates are handled by interrupt, just check if timer finished
                if (timer_minutes_int == 0 && timer_seconds_int == 0 && timer_active) {
                    timer_active = 0;
                    
                    // Switch between focus and rest
                    if (current_timer_type == FOCUS_TIME_SET_STEP) {
                        // Focus finished, start rest
                        start_timer(RESTING_TIME_COUNTER_STEP);
                        P2OUT |= BIT2;
                    } else {
                        // Rest finished, start focus again
                        start_timer(FOCUS_TIME_SET_STEP);
                       P2OUT |= BIT2;
                    }
                }
            }
        }
    }
}

void handle_welcome_step(){ 
    if (get_value("OK")) {
        // OK button detected - advance step
        currentStep = FOCUS_TIME_SET_STEP;
    }
}

void show_focus_display() {
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print_string("Foco: ");
    
    char minutes_display[3];
    minutes_display[0] = focus_minutes_tenth;
    minutes_display[1] = focus_minutes_unit;
    minutes_display[2] = '\0';
    
    lcd_print_string(minutes_display);
    lcd_print_string("min");
    lcd_set_cursor(1, 0);
    lcd_print_string("OK p/ continuar");
    
    if (isEditing == MINUTES_TENTH) {
        lcd_set_cursor(0, 6);
        lcd_cursor_blink_on();
    } else if (isEditing == MINUTES_UNIT) {
        lcd_set_cursor(0, 7);
        lcd_cursor_blink_on();
    }
}

void handle_focus_time_set_step() {
    bool inputProcessed = false;
    
    if (get_value("<")) {
        isEditing = MINUTES_TENTH;
        inputProcessed = true;
    } else if (get_value(">")) {
        isEditing = MINUTES_UNIT;
        inputProcessed = true;
    } else if (get_value("^")) {
        // Increment button - increase by 1
        increment_minutes();
        inputProcessed = true;
    } else if (get_value("V")) {
        // Decrement button - decrease by 1
        decrement_minutes();
        inputProcessed = true;
    } else if (get_value("OK")) {
        currentStep = RESTING_TIME_COUNTER_STEP;
        return; // Exit early, main loop will handle step transition
    } else if (get_value("0")) {
        (isEditing == 1) ? (focus_minutes_tenth = '0') : (focus_minutes_unit = '0');
        inputProcessed = true;
    } else if (get_value("1")) {
        (isEditing == 1) ? (focus_minutes_tenth = '1') : (focus_minutes_unit = '1');
        inputProcessed = true;
    } else if (get_value("2")) {
        (isEditing == 1) ? (focus_minutes_tenth = '2') : (focus_minutes_unit = '2');
        inputProcessed = true;
    } else if (get_value("3")) {
        (isEditing == 1) ? (focus_minutes_tenth = '3') : (focus_minutes_unit = '3');
        inputProcessed = true;
    } else if (get_value("4")) {
        (isEditing == 1) ? (focus_minutes_tenth = '4') : (focus_minutes_unit = '4');
        inputProcessed = true;
    } else if (get_value("5")) {
        (isEditing == 1) ? (focus_minutes_tenth = '5') : (focus_minutes_unit = '5');
        inputProcessed = true;
    } else if (get_value("6")) {
        (isEditing == 1) ? (focus_minutes_tenth = '6') : (focus_minutes_unit = '6');
        inputProcessed = true;
    } else if (get_value("7")) {
        (isEditing == 1) ? (focus_minutes_tenth = '7') : (focus_minutes_unit = '7');
        inputProcessed = true;
    } else if (get_value("8")) {
        (isEditing == 1) ? (focus_minutes_tenth = '8') : (focus_minutes_unit = '8');
        inputProcessed = true;
    } else if (get_value("9")) {
        (isEditing == 1) ? (focus_minutes_tenth = '9') : (focus_minutes_unit = '9');
        inputProcessed = true;
    }
    
    timer_minutes[0] = focus_minutes_tenth;
    timer_minutes[1] = focus_minutes_unit;
    timer_minutes[2] = '\n';

    // Only update display if we processed input and didn't change steps
    if (inputProcessed) {
        show_focus_display();
    }
}

void show_rest_display() {
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print_string("Descanso: ");
    
    char minutes_display[3];
    minutes_display[0] = resting_minutes_tenth;
    minutes_display[1] = resting_minutes_unit;
    minutes_display[2] = '\0';
    
    lcd_print_string(minutes_display);
    lcd_print_string("min");
    lcd_set_cursor(1, 0);
    lcd_print_string("OK p/ continuar");
    
    if (isEditing == MINUTES_TENTH) {
        lcd_set_cursor(0, 10);
        lcd_cursor_blink_on();
    } else if (isEditing == MINUTES_UNIT) {
        lcd_set_cursor(0, 11);
        lcd_cursor_blink_on();
    }
}

// Function to start the timer
void start_timer(int timer_type) {
    current_timer_type = timer_type;
    
    if (timer_type == FOCUS_TIME_SET_STEP) {
        // Set focus time
        timer_minutes_int = (focus_minutes_tenth - '0') * 10 + (focus_minutes_unit - '0');
    } else {
        // Set rest time
        timer_minutes_int = (resting_minutes_tenth - '0') * 10 + (resting_minutes_unit - '0');
    }
    
    timer_seconds_int = 0;
    timer_active = 1;
    
    // Initialize the countdown timer
    P2OUT |= BIT2; // liga o buzzer
    configure_countdown_timer();
    
    // Show initial display
    show_counter_display();
    // P2OUT &= ~BIT2; // desliga o buzzer
}

void show_counter_display() {
    lcd_cursor_blink_off();
    lcd_clear();
    lcd_set_cursor(0, 0);
    
    // Display current phase
    if (current_timer_type == FOCUS_TIME_SET_STEP) {
        lcd_print_string("FOCO!");
    } else {
        lcd_print_string("DESCANSO!");
    }
    
    lcd_set_cursor(1, 0);
    
    // Format and display time as MM:SS
    char time_display[6]; // "MM:SS\0"
    time_display[0] = (timer_minutes_int / 10) + '0';
    time_display[1] = (timer_minutes_int % 10) + '0';
    time_display[2] = ':';
    time_display[3] = (timer_seconds_int / 10) + '0';
    time_display[4] = (timer_seconds_int % 10) + '0';
    time_display[5] = '\0';
    
    lcd_print_string(time_display);
}

void handle_rest_time_set_step() {
    bool inputProcessed = false;
    
    if (get_value("<")) {
        isEditing = MINUTES_TENTH;
        inputProcessed = true;
    } else if (get_value(">")) {
        isEditing = MINUTES_UNIT;
        inputProcessed = true;
    } else if (get_value("^")) {
        // Increment button - increase by 1
        increment_minutes();
        inputProcessed = true;
    } else if (get_value("V")) {
        // Decrement button - decrease by 1
        decrement_minutes();
        inputProcessed = true;
    } else if (get_value("OK")) {
        currentStep = TIMER_STEP;
        return; // Exit early, main loop will handle step transition
    } else if (get_value("0")) {
        (isEditing == 1) ? (resting_minutes_tenth = '0') : (resting_minutes_unit = '0');
        inputProcessed = true;
    } else if (get_value("1")) {
        (isEditing == 1) ? (resting_minutes_tenth = '1') : (resting_minutes_unit = '1');
        inputProcessed = true;
    } else if (get_value("2")) {
        (isEditing == 1) ? (resting_minutes_tenth = '2') : (resting_minutes_unit = '2');
        inputProcessed = true;
    } else if (get_value("3")) {
        (isEditing == 1) ? (resting_minutes_tenth = '3') : (resting_minutes_unit = '3');
        inputProcessed = true;
    } else if (get_value("4")) {
        (isEditing == 1) ? (resting_minutes_tenth = '4') : (resting_minutes_unit = '4');
        inputProcessed = true;
    } else if (get_value("5")) {
        (isEditing == 1) ? (resting_minutes_tenth = '5') : (resting_minutes_unit = '5');
        inputProcessed = true;
    } else if (get_value("6")) {
        (isEditing == 1) ? (resting_minutes_tenth = '6') : (resting_minutes_unit = '6');
        inputProcessed = true;
    } else if (get_value("7")) {
        (isEditing == 1) ? (resting_minutes_tenth = '7') : (resting_minutes_unit = '7');
        inputProcessed = true;
    } else if (get_value("8")) {
        (isEditing == 1) ? (resting_minutes_tenth = '8') : (resting_minutes_unit = '8');
        inputProcessed = true;
    } else if (get_value("9")) {
        (isEditing == 1) ? (resting_minutes_tenth = '9') : (resting_minutes_unit = '9');
        inputProcessed = true;
    }
    
    // Only update display if we processed input and didn't change steps
    if (inputProcessed) {
        show_rest_display();
    }
}

bool get_value(char* button) {
    if (strcmp(button, "1") == 0) {
        return memcmp(irPulseBitsCmdHex, "A2", 2) == 0;
    }
    else if (strcmp(button, "2") == 0) {
        return memcmp(irPulseBitsCmdHex, "62", 2) == 0;
    }
    else if (strcmp(button, "3") == 0) {
        return memcmp(irPulseBitsCmdHex, "E2", 2) == 0;
    }
    else if (strcmp(button, "4") == 0) {
        return memcmp(irPulseBitsCmdHex, "22", 2) == 0;
    }
    else if (strcmp(button, "5") == 0) {
        return memcmp(irPulseBitsCmdHex, "02", 2) == 0;
    }
    else if (strcmp(button, "6") == 0) {
        return memcmp(irPulseBitsCmdHex, "C2", 2) == 0;
    }
    else if (strcmp(button, "7") == 0) {
        return memcmp(irPulseBitsCmdHex, "E0", 2) == 0;
    }
    else if (strcmp(button, "8") == 0) {
        return memcmp(irPulseBitsCmdHex, "A8", 2) == 0;
    }
    else if (strcmp(button, "9") == 0) {
        return memcmp(irPulseBitsCmdHex, "90", 2) == 0;
    }
    else if (strcmp(button, "0") == 0) {
        return memcmp(irPulseBitsCmdHex, "98", 2) == 0;
    }
    else if (strcmp(button, "*") == 0) {
        return memcmp(irPulseBitsCmdHex, "68", 2) == 0;
    }
    else if (strcmp(button, "#") == 0) {
        return memcmp(irPulseBitsCmdHex, "B0", 2) == 0;
    }
    else if (strcmp(button, "^") == 0) {
        return memcmp(irPulseBitsCmdHex, "18", 2) == 0;
    }
    else if (strcmp(button, "V") == 0) {
        return memcmp(irPulseBitsCmdHex, "4A", 2) == 0;
    }
    else if (strcmp(button, "<") == 0) {
        return memcmp(irPulseBitsCmdHex, "10", 2) == 0;
    }
    else if (strcmp(button, ">") == 0) {
        return memcmp(irPulseBitsCmdHex, "5A", 2) == 0;
    }
    else if (strcmp(button, "OK") == 0) {
        return memcmp(irPulseBitsCmdHex, "38", 2) == 0;
    }
    else {
        return false;
    }
}

void configure_receiver(void) {
    // 2. Configure P2.0 for IR input (TA1.CCI1A)
    P2DIR &= ~BIT0;    // Set P2.0 as input
    P2SEL |= BIT0;     // Select peripheral function for P2.0 (TA1.CCI1A)
    P2REN |= BIT0;     // Enable pull-up/pull-down resistor on P2.0
    P2OUT |= BIT0;     // Select pull-up resistor

    // Stop timer first
    TA1CTL = 0;

    // Clear timer and flags
    TA1CTL |= TACLR;
    TA1CCTL1 &= ~CCIFG;  // Clear capture flag

    // Configure timer: SMCLK, continuous mode, no divider
    TA1CTL = TASSEL_2 | MC_2 | ID_0 | TACLR;

    // Configure capture: falling edge, CCI1A, sync, capture mode
    TA1CCTL1 = CM_2 | CCIS_0 | SCS | CAP | CCIE;
}

// Modified timer initialization - add this to your configure_receiver() or create separate function
void configure_countdown_timer(void) {
    // If you have a 32768 Hz crystal (ACLK), this would be more accurate
    TA0CTL = 0;
    TA0CTL |= TACLR;
    
    // ACLK is typically 32768 Hz
    TA0CCR0 = 32767;  // 32768 - 1 for exactly 1 second
    TA0CTL = TASSEL_1 | MC_1 | TACLR;  // TASSEL_1 = ACLK
    TA0CCTL0 = CCIE;
}

void configure_msp_button(){
    //Configuração da chave S2 (P1.1) como entrada com resistor de pull-up com acionamento via interrupção
    P1DIR &= ~BIT1;  // Define P1.1 como entrada
    P1REN |= BIT1;   // Habilita o resistor de pull-up/pull-down
    P1OUT |= BIT1;   // Configura como pull-up (alto quando solto, baixo quando pressionado)
    P1IE  |= BIT1;   // Habilita Interrupção
    P1IES |= BIT1;   // Interrupção na borda de descida
    P1IFG &= ~BIT1;  // Limpa a flag de interrupção
}

void configure_buzzer(){
    P2DIR |= BIT2;
    P2REN |= BIT2;
    P2OUT &= ~BIT2;
}

void process_signal() {
    for (i = 0; i < 8; ++i){
        irPulseBitsAddr[i]    = irPulseBits[i];
        irPulseBitsAddrInv[i] = irPulseBits[i+8];
        irPulseBitsCmd[i]     = irPulseBits[i+16];
        irPulseBitsCmdInv[i]  = irPulseBits[i+24];
    }

    byteToHex(irPulseBitsAddr, irPulseBitsAddrHex);
    byteToHex(irPulseBitsCmd, irPulseBitsCmdHex);
    byteToHex(irPulseBitsAddrInv, irPulseBitsAddrInvHex);
    byteToHex(irPulseBitsCmdInv, irPulseBitsCmdInvHex);
}

void increment_minutes() {
    char minutes_tenth = (currentStep == RESTING_TIME_COUNTER_STEP) ? resting_minutes_tenth : focus_minutes_tenth;
    char minutes_unit = (currentStep == RESTING_TIME_COUNTER_STEP) ? resting_minutes_unit : focus_minutes_unit;
    int total_minutes = (minutes_tenth - '0') * 10 + (minutes_unit - '0');
    total_minutes = (total_minutes + 1) % 100; // Rollover at 100
    minutes_tenth = (total_minutes / 10) + '0';
    minutes_unit = (total_minutes % 10) + '0';
    if (currentStep == RESTING_TIME_COUNTER_STEP) {
        resting_minutes_tenth = minutes_tenth;
        resting_minutes_unit = minutes_unit;
    } else {
        focus_minutes_tenth = minutes_tenth;
        focus_minutes_unit = minutes_unit;
    }
}

void decrement_minutes() {
    char minutes_tenth = (currentStep == RESTING_TIME_COUNTER_STEP) ? resting_minutes_tenth : focus_minutes_tenth;
    char minutes_unit = (currentStep == RESTING_TIME_COUNTER_STEP) ? resting_minutes_unit : focus_minutes_unit;
    int total_minutes = (minutes_tenth - '0') * 10 + (minutes_unit - '0');
    total_minutes = (total_minutes - 1 + 100) % 100; // Rollover at -1
    minutes_tenth = (total_minutes / 10) + '0';
    minutes_unit = (total_minutes % 10) + '0';
        if (currentStep == RESTING_TIME_COUNTER_STEP) {
        resting_minutes_tenth = minutes_tenth;
        resting_minutes_unit = minutes_unit;
    } else {
        focus_minutes_tenth = minutes_tenth;
        focus_minutes_unit = minutes_unit;
    }
}

void decrement_timer(int step) {
    char minutes_tenth = (step == RESTING_TIME_COUNTER_STEP) ? resting_minutes_tenth : focus_minutes_tenth;
    char minutes_unit = (step == RESTING_TIME_COUNTER_STEP) ? resting_minutes_unit : focus_minutes_unit;
    int total_minutes = (minutes_tenth - '0') * 10 + (minutes_unit - '0');
    total_minutes = (total_minutes - 1 + 100) % 100; // Rollover at -1
    minutes_tenth = (total_minutes / 10) + '0';
    minutes_unit = (total_minutes % 10) + '0';
    if (step == RESTING_TIME_COUNTER_STEP) {
        resting_minutes_tenth = minutes_tenth;
        resting_minutes_unit = minutes_unit;
    } else if (step == FOCUS_TIME_SET_STEP) {
        focus_minutes_tenth = minutes_tenth;
        focus_minutes_unit = minutes_unit;
    }
    lcd_cursor_blink_off();
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print_string("FOCO!");
    lcd_set_cursor(1, 0);
    char minutes_display[3];
    minutes_display[0] = focus_minutes_tenth;
    minutes_display[1] = focus_minutes_unit;
    minutes_display[2] = '\0';
    lcd_print_string(minutes_display);
    lcd_clear();
}

void reset() {
    timer_active = 0;
    current_timer_type = FOCUS_TIME_SET_STEP;
    currentStep = 0;
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print_string("OK para escolher"); 
    lcd_set_cursor(1, 0);
    lcd_print_string("o tempo de foco"); 
    handle_welcome_step();
}

// Rotina de serviço de interrupção para a Porta 1 (botão S2)
#pragma vector=PORT1_VECTOR
__interrupt void Port1_ISR(void) {
    if (P1IFG & BIT1) {          // Verifica se a interrupção foi causada pelo botão S2
        __delay_cycles(20000);   // Debounce
        if (!(P1IN & BIT1)) {    // Confirma o pressionamento
            reset();
        }
    }
    P1IFG &= ~BIT1;          // Limpa a flag de interrupção
}

// Rotina de serviço de interrupção para a Porta 2 (botão S1)
#pragma vector=PORT2_VECTOR
__interrupt void Port2_ISR(void) {
    if (P2IFG & BIT1) {          // Verifica se a interrupção foi causada pelo botão S1
        __delay_cycles(20000);   // Debounce
        if (!(P2IN & BIT1)) {    // Confirma o pressionamento
            P2OUT &= ~BIT2;
        }
    }
    P2IFG &= ~BIT1;          // Limpa a flag de interrupção
}

static inline uint8_t pack_bits(const char bits[8]) {
    uint8_t v = 0;
    size_t i = 0;
    for (i = 0; i < 8; ++i) {
        v = (v << 1) | (bits[i] == 'U');
    }
    return v;
}

void byteToHex(const char input[8], char hex[3]) {
    uint8_t value = pack_bits(input);
    hex[0] = hexTable[value >> 4];  // High nibble (e.g., 0xF in 0xF1)
    hex[1] = hexTable[value & 0x0F]; // Low nibble (e.g., 0x1 in 0xF1)
    hex[2] = '\0';
}

// Timer_A1 Interrupt Service Routine
#pragma vector=TIMER1_A1_VECTOR
__interrupt void TIMER1_A1_ISR(void) {
    switch (__even_in_range(TA1IV, TA1IV_TAIFG)) {
        case TA1IV_TACCR1: // CCR1 Capture
            // Your existing capture logic here
            if (TA1CCR1 < PULSE_ZERO_TICKS) {
                irPulseBits[irBitCount++] = 'Z';
            } else if (TA1CCR1 < PULSE_ONE_TICKS) {
                irPulseBits[irBitCount++] = 'U';
            } else {
                irBitCount = 0;
            }
            TA1CTL |= TACLR;
            break;

        case TA1IV_TAIFG:  // Timer overflow
            TA1CTL |= TACLR;
            break;
    }

    // Clear the capture/compare interrupt flag
    TA1CCTL1 &= ~CCIFG;

    if (irBitCount >= 32) {
        TA1CCTL1 &= ~CCIE;  // Disable capture interrupt
        signalReady = 1;
    }
}

// Timer A0 interrupt service routine - replace or add this
#pragma vector=TIMER0_A0_VECTOR
__interrupt void TIMER0_A0_ISR(void) {
    if (timer_active) {
        // Decrement timer
        if (timer_seconds_int > 0) {
            if(timer_seconds_int == 60 - BUZZER_BEEP_DURATION) P2OUT &= ~BIT2;
            timer_seconds_int--;
        } else if (timer_minutes_int > 0) {
            timer_minutes_int--;
            timer_seconds_int = 59;
        } else {
            // Timer reached zero
            timer_active = 0;
        }
        
        // Update display
        show_counter_display();
    }
}