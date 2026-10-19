/*
 * Safe GPIO setup for the RF2000v2 I851 CTC V3 ATmega2560 board.
 * Wiring reference: supplied Repetier RF2000v2 configuration; by user
 * direction it takes precedence when schematic assignments disagree.
 * GPL-3.0-or-later.
 */
#include "grbl.h"
#include <util/atomic.h>

#ifdef CPU_MAP_2560_RENKFORCE_CTC_V3

#define RF2000_OFF_A (_BV(PA3)|_BV(PA5))             // OUT1, OUT2
#define RF2000_OFF_B _BV(PB4)                       // HZ1
#define RF2000_OFF_C (_BV(PC4)|_BV(PC5))             // OUT3, OUT4
#define RF2000_OFF_E (_BV(PE2)|_BV(PE3)|_BV(PE5))    // HZ3, buzzer, P230
#define RF2000_OFF_F _BV(PF4)                       // OUT5 / case fan
#define RF2000_OFF_H (_BV(PH3)|_BV(PH4)|_BV(PH5)|_BV(PH6)) // RGB, HZ2

void rf2000_board_outputs_off(void)
{
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    PORTA &= ~RF2000_OFF_A;
    PORTB &= ~RF2000_OFF_B;
    PORTC &= ~RF2000_OFF_C;
    PORTE &= ~RF2000_OFF_E;
    PORTF &= ~RF2000_OFF_F;
    PORTH &= ~RF2000_OFF_H;
  }
}

void rf2000_board_init(void)
{
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    // PF4..7 are occupied by motor/fan GPIO. Release JTAG even if its fuse is
    // enabled. AVR requires two JTD writes within four CPU cycles.
    uint8_t mcucr = MCUCR | _BV(JTD);
    MCUCR = mcucr;
    MCUCR = mcucr;

    // Disconnect ALL timer compare outputs before asserting GPIO OFF. Timers
    // 0/1, 2 and 3 are configured later for pulses, safety polling and sleep;
    // none of their OC pins are permitted to drive a heater or auxiliary load.
    TCCR0A = 0;
    TCCR1A = 0;
    TCCR2A = 0;
    TCCR3A = 0;
    TCCR4A = 0;
    TCCR5A = 0;
    // Do not inherit interrupt sources from a bootloader before UART-only
    // initialization. Each required module subsequently enables its timer.
    TIMSK0 = 0;
    TIMSK1 = 0;
    TIMSK2 = 0;
    TIMSK3 = 0;
    TIMSK4 = 0;
    TIMSK5 = 0;
    EIMSK = 0;
    PCICR = 0;
    ADCSRA = 0; // No thermistor or other ADC sampling.
    SPCR = 0;   // No hardware SPI/SD traffic (DRV8711 uses separate soft SPI).
    UCSR1B = 0;
    UCSR2B = 0; // PH1 is Z-MAX, not UART2 TX.
    UCSR3B = 0; // Keep UART0 exclusively for the GRBL protocol.

    // Preload the latches BEFORE enabling outputs, so no heater/FET can flash
    // on while changing its pin from input to output.
    rf2000_board_outputs_off();
    DDRA |= RF2000_OFF_A;
    DDRB |= RF2000_OFF_B;
    DDRC |= RF2000_OFF_C;
    DDRE |= RF2000_OFF_E;
    DDRF |= RF2000_OFF_F;
    DDRH |= RF2000_OFF_H;

    // Repetier RF2000v2 assigns WATCHDOG_PIN to PA7/D29. Match its
    // HAL::stopWatchdog(): release WDI as an input without pull-up.
    // This test profile does not generate a watchdog heartbeat. PC4 is OUT3.
    PORTA &= ~_BV(PA7);
    DDRA &= ~_BV(PA7);

    // Optional Y-test LCD/keypad is configured later by rf2000_panel_init().
    // Servos, temperature, SD and I2C/load cells remain unconfigured.
    // DRV8711 owns all five nSLEEP, STEP/DIR, CS and reset lines. Limits owns
    // PC2/PC0/PC6/PH1 and enables its pull-ups before permitting motion.
  }
}

#endif
