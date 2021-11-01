#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>
#include <stdio.h>
#include <util/twi.h>





int main(void)
{

    DDRD = 0b00100000; //PD6 is output for LED
	PORTD = 0b00001000; // Enables internal Pull-Up Resistor on PB


    // setting timer for button read

    TCCR0A |= (1 << WGM01);                      // Sets Timer0 in CTC mode
    OCR0A = 249;                                 // Numbers of ticks in 1ms for 16 MHz / 64
    TIMSK0 = (1 << OCIE0A);

    sei();                                       // Enables global interupts
    
    TCCR0B |= (1 << CS00) | (1 << CS01);         // Prescaler = 64 and starts counting



    while(1)
    {
    }

}