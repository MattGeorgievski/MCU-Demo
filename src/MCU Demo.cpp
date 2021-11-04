#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>
#include <stdio.h>
#include <util/twi.h>


// variables for ADC
#define fCPU 16000000
#define baud 9600
#define brc ((fCPU/16/baud) - 1)

volatile float ADCvalue;
int ADCprint;


// lower 4 bit definitions for LCD control
#define LCD_RS 	0       // Register select (0-instruction, 1-data)
#define LCD_RW 	1       // Read (1) or Write (0)
#define LCD_EN 	2       // Enable (1->0) transition fetchs data
#define LCD_BL 	3       // LCD Backlight

// TWI functions to send a byte
void init_twi();
int putchar_twi(uint8_t ch);

// LCD+TWI functions to send a byte/string 
void init_lcd();
void lcd_light(int on);
void putchar_lcd(uint8_t ch, int rs);
void puts_lcd(char *s);

void putchar_uart(char ch);
void puts_uart(char *s);
void puts_uart_int(char *s);

void updateADC();
void stateMachine(int stateInput);
void clearLCD();
void setPosStart();


// Delay functions used to interface LCD
void delay1ms();
void delay15ms();

volatile unsigned long msElapsed = 0;
static bool prev = false;
volatile int stateCount = 0;
volatile bool btn_pressed;
volatile bool reliable;
bool isClear = true;
volatile int timerFlag;
int state = 0;
unsigned char tx_buf[100];
unsigned int tx_index=0, tx_size=0;
volatile unsigned long msElapsedUART = 0;
volatile bool uartFlag = true;
volatile bool secondFlag = false;
volatile unsigned long thousandms = 0;
bool pushButton2;
volatile int ratioCount = 0;
volatile bool ratioFlag = false;
volatile float ratio;

char ADCstring[50] = ("ADC:val  STATE BMECHATRONICS 1");
char string[50] = ("SID:13894023    MECHATRONICS 1");
char ADCupdate[10] = ("ADC:");
char uartString[50] = ("S2021 EMS SID: 13894023, ADC Reading: %d");


ISR(TIMER0_COMPA_vect)
{

    msElapsed++;                                // Increases count every ms
    msElapsedUART++;
    thousandms++;
    ratioCount++;
    
    if(msElapsed > 200)                        // when 200 ms has elapsed
    {
        btn_pressed = (PIND & 0b00001000);
        reliable = btn_pressed==prev;
        prev = btn_pressed;
        msElapsed = 0;
    }

    if(!reliable)
    {
        btn_pressed = false;
    }                          

    if(!btn_pressed)
    {
        stateCount++;
    }

    if(!(stateCount % 2 == 0))
    {
        PORTD |= (1<<7);
        state = 0;


    }
    else if(stateCount % 2 == 0)
    {

        PORTD &= ~(1<<7);
        state = 1;


    }

    
    if(msElapsedUART > 500)
    {

        uartFlag = true;

        if(!state & uartFlag)
        {
            puts_uart(uartString);
            delay15ms();
            uartFlag = false;
        }

        if(state)
        {

            uartFlag = false;

        }

        msElapsedUART = 0;
    }

    ratio = (float) ((ADCvalue*800/1023) + 200);

    if((ratioCount > ratio) & state)
    {

                PORTD ^= (1 << PD6);
                ratioCount = 0;

    }

}

ISR(ADC_vect)
{
    ADCvalue = ADCL | ADCH << 8;
    

}

int main(void)
{

    DDRD = (1 << PD7) | (1 << PD4) | (1 << PD5) | (1 << PD6); //PD6 is output for state LED, PD5, PD7, PD3
	PORTD = (1 << PORTD3) | (1 << PORTD2); // Enables internal Pull-Up Resistor on PB


    // setting timer for button read

    TCCR0A |= (1 << WGM01);                      // Sets Timer0 in CTC mode
    OCR0A = 249;                                 // Numbers of ticks in 1ms for 16 MHz / 64
    TIMSK0 = (1 << OCIE0A);

    sei();                                       // Enables global interupts
    
    TCCR0B |= (1 << CS00) | (1 << CS01);         // Prescaler = 64 and starts counting

    UCSR0B = (1 << TXEN0) | (1 << RXEN0);  // Enables Tx and Rx
    UCSR0C = (1 << UCSZ00) | (1 << UCSZ01); // Sets data size to 8 bits

    



    init_twi();
    init_lcd();

    // ADC start
    ADMUX |= (1 << REFS0) | (1 << MUX0);
    ADCSRA |= (1 << ADPS2) | (1 << ADIE);
    
	
    UBRR0H = (brc >> 8);
    UBRR0L = brc;   // sets baud rate




    while(1)
    { 
        updateADC();
        stateMachine(state);

        if(UCSR0A & (1 << RXC0))
        {
            char keyboardInput = UDR0;

            if(keyboardInput == 'g')
            {

                UCSR0B ^= (1 << TXEN0);
            }
        }

        
    }

}

void init_twi()
{
	PORTC = (1<<DDC5)|(1<<DDC4);	// Activate the pull-up resistors for SDA/SCL(TWI).
                                    // Otherwise, you need to attach external pull-ups
	
    TWSR = (1<<TWPS1)|(1<<TWPS0);   // PS-64, sets TWI clock to 1Kbps
	TWBR = 124;                     // Bit rate: TWBR = ((F_CPU / 1000) - 16) / (2*PS)
    TWDR = 0xFF;                    // Default content = SDA released.
	TWCR = (1<<TWEN);               // Enable TWI-interface and release TWI pins.
}

int putchar_twi(uint8_t ch)
{
	uint8_t addr = 0x27;            // LCD default address (0x27). Check your LCD board

	// 1. Start (From Table 26.2, page 270, Atmega328p datasheet (single chip))
	TWCR = (1<<TWINT) | (1<<TWSTA) | (1<<TWEN) | (1<<TWEA);
	while (!(TWCR & (1<<TWINT)));
	if ((TWSR & 0xF8) != TW_START)
	return -1;
	
	// 2. Send SLA+W (Write Mode)
	TWDR = (addr << 1) | (TW_WRITE);	// SLA+W
	TWCR = (1<<TWINT)|(1<<TWEN);		// Start transmission
	while (!(TWCR & (1<<TWINT)));
	if ((TWSR & 0xF8) != TW_MT_SLA_ACK)
	return -2;

	//	3. Send Data #1 (actual data)
	TWDR = ch;							// Data (at the sub-address register)
	TWCR = (1<<TWINT)|(1<<TWEN);		// Start transmission
	while (!(TWCR & (1<<TWINT)));
	if ((TWSR & 0xF8) != TW_MT_DATA_ACK)
	return -4;

	// 4. Stop condition
	TWCR = (1<<TWINT)|(1<<TWEN)|(1<<TWSTO);
	
	return 0;
}


void init_lcd()
{
    uint8_t ch; 
          
    // Atmega hardware reset doesn't affect LCD, so we do software reset here (32ms+)
    // function set 1 (repeat 3 times). Ref) Fig. 24 @ Page 46, HD44780U datasheet
    delay15ms();
    delay15ms();
    ch = 0x30|(1<<LCD_BL);      // reset (0x30)
    for (int i=0; i<3; i++){
        ch |= (1<<LCD_EN);
        putchar_twi(ch);
        delay15ms();
        ch &= ~(1<<LCD_EN);
        putchar_twi(ch);
        delay15ms();
    }
    
    // Send command (0x20) to set LCD to 4-bit mode
    // Ref) Table 12 @ Page 42, HD44780U datasheet
	ch = 0x20|(1<<LCD_BL);
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();
	
    // [TASK 2] Complete the code:
    // Send commands to instruction register (RS=0) 
    // to initialise LCD. A byte is splited into two 4-bits
    // Ref) Table 12 @ Page 42, HD44780U datasheet
    // Ref) Fig. 11 @ Page 28, HD44780U datasheet
    // The instructions to be sent in sequence are:
    // - (set 2-line mode with font size)
    // - (display off)
    // - (clear display)
    // - (set entry mode)
    // - (display on and cursor blinking)


    // used: http://web.alfredstate.edu/faculty/weimandn/lcd/lcd_initialization/lcd_initialization_index.html
    // followed steps for remainder of LCD init
    
    ch = 0x20|(1<<LCD_BL); // step 6
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();

    ch = 0x80|(1<<LCD_BL); //step 6
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();

    ch = 0x00|(1<<LCD_BL); //step 7
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();

    ch = 0x80|(1<<LCD_BL); //step 7
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();

    ch = 0x00|(1<<LCD_BL); //step 8
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();

    ch = 0x10|(1<<LCD_BL); //step 8
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay15ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay15ms();

    ch = 0x00|(1<<LCD_BL); //step 9
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();

    ch = 0xB0|(1<<LCD_BL); //step 9
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();

    ch = 0x00|(1<<LCD_BL); //step 11
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();

    ch = 0xC0|(1<<LCD_BL); //step 11
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();


}

void clearLCD()
{
    uint8_t ch;

    ch = 0x00|(1<<LCD_BL); //step 8
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();

    ch = 0x10|(1<<LCD_BL); //step 8
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay15ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay15ms();


}

void putchar_lcd(uint8_t ch, int rs)
{
	
    uint8_t lcdData;

    lcdData = (ch & 0xF0) | (1 << LCD_BL) | (1 << LCD_EN);
    lcdData &= ~(1 << LCD_RW);
    
    if(rs == 1)
    {
        lcdData |= (1 << LCD_RS);
    }
    else if(rs == 0)
    {
        lcdData &= ~(1 << LCD_RS);
    }

    putchar_twi(lcdData);

    delay1ms();

    lcdData &= ~(1 << LCD_EN);

    putchar_twi(lcdData);

    delay1ms();

    lcdData = ((ch << 4) & 0xF0) | (1 << LCD_BL) | (1 << LCD_EN);
    lcdData &= ~(1 << LCD_RW);
    
    if(rs)
    {
        lcdData |= (1 << LCD_RS);
    }
    else if(rs == 0)
    {
        lcdData &= ~(1 << LCD_RS);
    }

    putchar_twi(lcdData);

    delay1ms();

    lcdData &= ~(1 << LCD_EN);

    putchar_twi(lcdData);

    delay1ms();

  
}

void puts_lcd(char *s)
{
    // [TASK 3] Complete this code after completing putchar_lcd()
    // send an instruction of the cursor position (e.g. 2nd row 1st column)
    // then send the string data to data register - call putchar_lcd() in a loop
    
    unsigned long i;
    int posCount = 0;

    for (i = 0; i < strlen(s); i++)
    {
        if(posCount == 16)
        {
            putchar_lcd(0xC0, 0);  // makes a new line at end of given first line which is 12 characters
        }

        putchar_lcd(s[i], 1);

        posCount++;

        
    }
    

}

void lcd_light(int on)
{
    char temp = 0x0|(1<<LCD_RS);   // write to data reg

	if (on==1){
        temp |= (1<<LCD_BL);      // back light bit on
    }
 	putchar_twi(temp);
	delay1ms();
 }


void delay1ms()
{
  TCCR2B = 0x04;
  while(!TIFR2);
  TCCR2B = 0x00; 
  TCNT2 = 0;  
  TIFR2 |= 0x01;
}

void delay15ms()


{
  TCCR2B = 0x07;
  while(!TIFR2);
  TCCR2B = 0x00;  
  TCNT2 = 0;  
  TIFR2 |= 0x01;
}

void updateADC()
{
    ADCSRA |= (1<<ADEN);
    ADCSRA |= (1<<ADSC); // start conversion
    ADCprint = ADCvalue;
    sprintf(ADCupdate, "ADC:%d ", ADCprint);
    ADCSRA &= ~(1<<ADEN);

}

void stateMachine(int stateInput)
{
    
    pushButton2 = (PIND & (1 << PD2));
    
    
    switch(stateInput)
    
    {
        case 0:

            PORTD &= ~(1 << PD4);
            PORTD &= ~(1 << PD5);
            PORTD &= ~(1 << PD6);
            sprintf(uartString, "S2021 EMS SID: 13894023, ADC Reading: %d", ADCprint);


            
            if(isClear)
            {
                setPosStart();
                puts_lcd(string);
                isClear = false;
                
            }
            break;

        case 1:



            if(!isClear)
            {
                clearLCD();
                isClear = true;
                puts_lcd(ADCstring);
            }

            if(state)
            {
                setPosStart();
                puts_lcd(ADCupdate);
                isClear = true;

            }

            if((thousandms > 1000) & !pushButton2)
            {
                PORTD &= ~(1 << PD5);
                PORTD ^= (1 << PD4);
                thousandms = 0;

            }

            if((thousandms > 1000) & pushButton2)
            {
                PORTD &= ~(1 << PD4);
                PORTD ^= (1 << PD5);
                thousandms = 0;

            }

            if(ratioFlag)
            {

                PORTD ^= (1 << PD6);
                ratioCount = 0;
                ratioFlag = false;

            }
            
            
            break;

    }
}

void setPosStart()
{

    uint8_t ch; 

    ch = 0x00|(1<<LCD_BL);
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();

    ch = 0x20|(1<<LCD_BL);
 	ch |= (1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();
	ch &= ~(1<<LCD_EN);
	putchar_twi(ch);
	delay1ms();

}

void putchar_uart(char ch)
{
    // [TASK 1] complete this function to send a char
    // using a polling method. You need to check the
    // UCSR0A register to wait until UDRE0-bit sets -
    // (UART (TX) Data Register Empty) 
    // Then you can write a byte to UDR0 

   while(1)
   {
       if(UCSR0A & (1 << UDRE0))
        {
            UDR0 = ch;
            break;
        }
   }
}

void puts_uart(char *s)
{
    // [TASK 1] send a string data using while() or for-loop(). 
    // note that the end of string is marked by '\0' (or 0x00)
    // Loop until the content is 0x00 (NULL). You can call 
    // putchar_uart() in the loop.
    


    unsigned long i;
    for(i = 0; i < strlen(s); i++)
    {
        putchar_uart(s[i]);
    }

    putchar_uart('\n');

}