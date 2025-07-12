// Conexões:
// Receptor IR: GND -> GND / VCC -> 3.3V / Signal -> P2.0
// Display LCD: GND -> GND / VCC -> 5V / SDA -> P3.0 / SCL -> P3.1
// Buzzer:      GND -> GND / Out -> P2.2

// Como funciona:
// Tela Inicial: aguarda o clique do botão OK no controle para avançar
// Telas de Configuração do Tempo:
//      < e > controlam se vai alterar a dezena ou a unidade dos minutos
//      ^ e V subtraem ou somam em 1 a minutagem atual. 0 -> 1 -> 2 -> ... -> 99 -> 0 -> 1
//      números controlam individualmente a dezena ou a unidade

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
int previousStep = WELCOME_STEP;

volatile int shouldBeep = 0;

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

int isEditing = MINUTES_TENTH;

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
void start_timer(int timer_type);

void main(void) {
    WDTCTL = WDTPW | WDTHOLD; // Stop watchdog timer

    configure_msp_button();
    configure_receiver();
    configure_buzzer();

    configure_lcd();
    clear_lcd_screen();
    position_lcd_cursor(0, 0);
    print_message("OK para escolher"); 
    position_lcd_cursor(1, 0);
    print_message("o tempo de foco");  

    __enable_interrupt();   // Habilita interrupções

    while (1) {
        if (currentStep <= RESTING_TIME_COUNTER_STEP) {
            if (signalReady) {
                process_signal();
                
                if (currentStep == WELCOME_STEP) {
                    if(timer_active) P2OUT |= BIT2;
                    handle_welcome_step();
                } else if (currentStep == FOCUS_TIME_SET_STEP) {
                    handle_focus_time_set_step();
                } else if (currentStep == RESTING_TIME_COUNTER_STEP) {
                    handle_rest_time_set_step();
                } 
                
                // Handler pra troca de steps não bugar
                if (currentStep != previousStep) {
                    if (currentStep == FOCUS_TIME_SET_STEP) {
                        show_focus_display();
                    } else if (currentStep == RESTING_TIME_COUNTER_STEP) {
                        show_rest_display();
                    }
                }
                
                signalReady = 0;
                irBitCount = 0;
                TA1CCTL1 &= ~CCIFG; // Limpa flag de captura
                TA1CCTL1 |= CCIE;
            }
        } else {
            if (currentStep == TIMER_STEP) {
                // Se for o primeiro ciclo de timer (desativado), liga o timer
                if (!timer_active) {
                    start_timer(FOCUS_TIME_SET_STEP);
                }
                
                shouldBeep = 1;
                // Se o tempo tiver chegado a 00:00 e estiver ativo, troca o timer atual
                if (timer_minutes_int == 0 && timer_seconds_int == 0 && timer_active) {
                    timer_active = 0;
                    if (current_timer_type == FOCUS_TIME_SET_STEP) {
                        start_timer(RESTING_TIME_COUNTER_STEP);
                        if (shouldBeep) P2OUT |= BIT2;
                    } else {
                        start_timer(FOCUS_TIME_SET_STEP);
                       if (shouldBeep) P2OUT |= BIT2;
                    }
                }
            }
        }
    }
}

void handle_welcome_step(){ 
    if (get_value("OK")) {
        currentStep = FOCUS_TIME_SET_STEP;
    }
}

void show_focus_display() {
    clear_lcd_screen();
    position_lcd_cursor(0, 0);
    print_message("Foco: ");
    
    char minutes_display[3];
    minutes_display[0] = focus_minutes_tenth;
    minutes_display[1] = focus_minutes_unit;
    minutes_display[2] = '\0';
    
    print_message(minutes_display);
    print_message("min");
    position_lcd_cursor(1, 0);
    print_message("OK p/ continuar");
    
    if (isEditing == MINUTES_TENTH) {
        position_lcd_cursor(0, 6);
        blink_cursor();
    } else if (isEditing == MINUTES_UNIT) {
        position_lcd_cursor(0, 7);
        blink_cursor();
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
        increment_minutes();
        inputProcessed = true;
    } else if (get_value("V")) {
        decrement_minutes();
        inputProcessed = true;
    } else if (get_value("OK")) {
        currentStep = RESTING_TIME_COUNTER_STEP;
        return;
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

    // Força atualização do lcd só se o input tiver sido processado por completo
    if (inputProcessed) {
        show_focus_display();
    }
}

void show_rest_display() {
    clear_lcd_screen();
    position_lcd_cursor(0, 0);
    print_message("Descanso: ");
    
    char minutes_display[3];
    minutes_display[0] = resting_minutes_tenth;
    minutes_display[1] = resting_minutes_unit;
    minutes_display[2] = '\0';
    
    print_message(minutes_display);
    print_message("min");
    position_lcd_cursor(1, 0);
    print_message("OK p/ continuar");
    
    if (isEditing == MINUTES_TENTH) {
        position_lcd_cursor(0, 10);
        blink_cursor();
    } else if (isEditing == MINUTES_UNIT) {
        position_lcd_cursor(0, 11);
        blink_cursor();
    }
}

void start_timer(int timer_type) {
    current_timer_type = timer_type;
    
    if (timer_type == FOCUS_TIME_SET_STEP) {
        timer_minutes_int = (focus_minutes_tenth - '0') * 10 + (focus_minutes_unit - '0');
    } else {
        timer_minutes_int = (resting_minutes_tenth - '0') * 10 + (resting_minutes_unit - '0');
    }
    
    timer_seconds_int = 0;
    timer_active = 1;
    
    configure_countdown_timer();
    show_counter_display();
}

void show_counter_display() {
    stop_blinking_cursor();
    clear_lcd_screen();
    position_lcd_cursor(0, 0);
    
    if (current_timer_type == FOCUS_TIME_SET_STEP) {
        print_message("FOCO!");
    } else {
        print_message("DESCANSO!");
    }
    
    position_lcd_cursor(1, 0);
    
    char time_display[6]; // "MM:SS\0"
    time_display[0] = (timer_minutes_int / 10) + '0';
    time_display[1] = (timer_minutes_int % 10) + '0';
    time_display[2] = ':';
    time_display[3] = (timer_seconds_int / 10) + '0';
    time_display[4] = (timer_seconds_int % 10) + '0';
    time_display[5] = '\0';
    
    print_message(time_display);
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
        increment_minutes();
        inputProcessed = true;
    } else if (get_value("V")) {
        decrement_minutes();
        inputProcessed = true;
    } else if (get_value("OK")) {
        currentStep = TIMER_STEP;
        return;
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
    
    // Força atualização do lcd só se o input tiver sido processado por completo
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
    P2DIR &= ~BIT0;      // P2.0 como input
    P2SEL |= BIT0;       // Periférico 
    P2REN |= BIT0;       // Habilita resistor
    P2OUT |= BIT0;       // de pull-up

    TA1CTL = 0;          // Reseta o timer

    TA1CTL |= TACLR;     // Limpa o timer
    TA1CCTL1 &= ~CCIFG;  // Limpa as flags de captura

    TA1CTL = TASSEL_2 | MC_2 | ID_0 | TACLR; // SMCLOCK em modo contínuo

    TA1CCTL1 = CM_2 | CCIS_0 | SCS | CAP | CCIE; // Configure modo de captura como borda de descida, CCI1A e síncrono
}

void configure_countdown_timer(void) {
    TA0CTL = 0;
    TA0CTL |= TACLR;
    
    // ACLK ~= 32768 Hz
    TA0CCR0 = 32767;  // 32768 - 1 = 1Hz
    TA0CTL = TASSEL_1 | MC_1 | TACLR;  // TASSEL_1 = ACLK
    TA0CCTL0 = CCIE;
}

void configure_msp_button(){
    P1DIR &= ~BIT1;  // P1.1 como input
    P1REN |= BIT1;   // Habilita o resistor
    P1OUT |= BIT1;   // como pull-up
    P1IE  |= BIT1;   // Habilita interrupção
    P1IES |= BIT1;   // como borda de descida
    P1IFG &= ~BIT1;  // Limpa a flag de interrupção
}

void configure_buzzer(){
    P2DIR |= BIT2;  // P2.2 como output
    P2REN |= BIT2;  // Habilita resistor
    P2OUT &= ~BIT2; // como pull-down
}

void process_signal() {
    // Quebra o sinal e atribui a cada variável o valor de sua responsabilidade
    for (i = 0; i < 8; ++i){
        irPulseBitsAddr[i]    = irPulseBits[i];
        irPulseBitsAddrInv[i] = irPulseBits[i+8];
        irPulseBitsCmd[i]     = irPulseBits[i+16];
        irPulseBitsCmdInv[i]  = irPulseBits[i+24];
    }

    // Traduz os pulsos de bits para hexadecimal
    byteToHex(irPulseBitsAddr, irPulseBitsAddrHex);
    byteToHex(irPulseBitsCmd, irPulseBitsCmdHex);
    byteToHex(irPulseBitsAddrInv, irPulseBitsAddrInvHex);
    byteToHex(irPulseBitsCmdInv, irPulseBitsCmdInvHex);
}

void increment_minutes() {
    char minutes_tenth = (currentStep == RESTING_TIME_COUNTER_STEP) ? resting_minutes_tenth : focus_minutes_tenth;
    char minutes_unit = (currentStep == RESTING_TIME_COUNTER_STEP) ? resting_minutes_unit : focus_minutes_unit;
    int total_minutes = (minutes_tenth - '0') * 10 + (minutes_unit - '0');
    total_minutes = (total_minutes + 1) % 100; // Rollover em 100
    minutes_tenth = (total_minutes / 10) + '0';
    minutes_unit = (total_minutes % 10) + '0';
    if (minutes_tenth == '0' && minutes_unit == '0') { // pula o 00
        minutes_tenth = '0';
        minutes_unit = '1';
    }
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
    total_minutes = (total_minutes - 1 + 100) % 100; // Rollover em -1
    minutes_tenth = (total_minutes / 10) + '0';
    minutes_unit = (total_minutes % 10) + '0';
    if (minutes_tenth == '0' && minutes_unit == '0') { // pula o 00
        minutes_tenth = '9';
        minutes_unit = '9';
    }
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
    total_minutes = (total_minutes - 1 + 100) % 100; // Rollover em -1
    minutes_tenth = (total_minutes / 10) + '0';
    minutes_unit = (total_minutes % 10) + '0';
    if (step == RESTING_TIME_COUNTER_STEP) {
        resting_minutes_tenth = minutes_tenth;
        resting_minutes_unit = minutes_unit;
    } else if (step == FOCUS_TIME_SET_STEP) {
        focus_minutes_tenth = minutes_tenth;
        focus_minutes_unit = minutes_unit;
    }
    stop_blinking_cursor();
    clear_lcd_screen();
    position_lcd_cursor(0, 0);
    print_message("FOCO!");
    position_lcd_cursor(1, 0);
    char minutes_display[3];
    minutes_display[0] = focus_minutes_tenth;
    minutes_display[1] = focus_minutes_unit;
    minutes_display[2] = '\0';
    print_message(minutes_display);
    clear_lcd_screen();
}

void reset() {
    stop_blinking_cursor();
    currentStep = WELCOME_STEP;
    previousStep = WELCOME_STEP;

    shouldBeep = 0;

    focus_minutes_tenth = '0';
    focus_minutes_unit = '1';

    resting_minutes_tenth = '0';
    resting_minutes_unit = '1';

    timer_minutes = "01";
    timer_seconds = "00";

    timer_minutes_int = 0;
    timer_seconds_int = 0;
    timer_active = 0;
    current_timer_type = FOCUS_TIME_SET_STEP;

    isEditing = MINUTES_TENTH;
    
    clear_lcd_screen();
    position_lcd_cursor(0, 0);
    print_message("OK para escolher"); 
    position_lcd_cursor(1, 0);
    print_message("o tempo de foco"); 
}

static inline uint8_t pack_bits(const char bits[8]) {
    uint8_t packed_bits = 0;
    size_t i = 0;
    for (i = 0; i < 8; ++i) {
        packed_bits = (packed_bits << 1) | (bits[i] == 'U');
    }
    return packed_bits;
}

void byteToHex(const char input[8], char hex[3]) {
    uint8_t value = pack_bits(input);
    // MSHex
    hex[0] = hexTable[value >> 4];
    // LSHex
    hex[1] = hexTable[value & 0x0F];
    // Terminador de string
    hex[2] = '\0';
}

// Interrupção do botão
#pragma vector=PORT1_VECTOR
__interrupt void Port1_ISR(void) {
    if (P1IFG & BIT1) {                     // Verifica se a interrupção foi causada pelo botão S2
        __delay_cycles(20000);      // Debounce
        if (!(P1IN & BIT1)) {               // Confirma o pressionamento
            reset();
        }
    }
    P1IFG &= ~BIT1;                         // Limpa a flag de interrupção
}

// Interrupção do timer do receptor IR
#pragma vector=TIMER1_A1_VECTOR
__interrupt void TIMER1_A1_ISR(void) {
    switch (__even_in_range(TA1IV, TA1IV_TAIFG)) {
        case TA1IV_TACCR1: // Captura de CCR1
            if (TA1CCR1 < PULSE_ZERO_TICKS) {
                irPulseBits[irBitCount++] = 'Z';
            } else if (TA1CCR1 < PULSE_ONE_TICKS) {
                irPulseBits[irBitCount++] = 'U';
            } else {
                irBitCount = 0;
            }
            TA1CTL |= TACLR;
            break;
        case TA1IV_TAIFG:  // Overflow do timer
            TA1CTL |= TACLR;
            break;
    }

    TA1CCTL1 &= ~CCIFG;     // Limpa a flag de interrupção

    if (irBitCount >= 32) {
        TA1CCTL1 &= ~CCIE;  // Desabilita a interrupção de captura
        signalReady = 1;
    }
}

// Interrupção do timer do pomodoro (1Hz)
#pragma vector=TIMER0_A0_VECTOR
__interrupt void TIMER0_A0_ISR(void) {
    if (timer_active) {
        // Decrementa o timer em 1s
        if (timer_seconds_int > 0) {
            // Liga o buzzer por 3s
            if(timer_seconds_int == 60 - BUZZER_BEEP_DURATION) P2OUT &= ~BIT2;
            timer_seconds_int--;
        } else if (timer_minutes_int > 0) {
            timer_minutes_int--;
            timer_seconds_int = 59;
        } else {
            // Timer em 00
            timer_active = 0;
        }
        
        // Atualiza a contagem no display
        show_counter_display();
    }
}