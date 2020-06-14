// CONFIG
#pragma config FOSC = INTOSCIO  // Oscillator Selection bits (INTOSCIO oscillator: I/O function on RA4/OSC2/CLKOUT pin, I/O function on RA5/OSC1/CLKIN)
#pragma config WDTE = OFF       // Watchdog Timer Enable bit (WDT disabled)
#pragma config PWRTE = OFF      // Power-up Timer Enable bit (PWRT disabled)
#pragma config MCLRE = OFF      // MCLR Pin Function Select bit (MCLR pin function is digital input, MCLR internally tied to VDD)
#pragma config CP = OFF         // Code Protection bit (Program memory code protection is disabled)
#pragma config CPD = OFF        // Data Code Protection bit (Data memory code protection is disabled)
#pragma config BOREN = OFF      // Brown Out Detect (BOR disabled)
#pragma config IESO = OFF       // Internal External Switchover bit (Internal External Switchover mode is disabled)
#pragma config FCMEN = OFF      // Fail-Safe Clock Monitor Enabled bit (Fail-Safe Clock Monitor is disabled)

#include "head.h"

// for millisecond timer
#define TIMER0_INITIAL 132


// shadow register for PortA, so as to not suffer from read/modify/write errors
volatile union
{
    unsigned char byte;
    struct
    {
        unsigned RA0:1;
        unsigned RA1:1;
        unsigned RA2:1;
        unsigned RA3:1;
        unsigned RA4:1;
        unsigned RA5:1;
    } bits;
    struct
    {
        unsigned DAT_LED:1;
        unsigned CLK_LED:1;
        unsigned IND_LED:1;
        unsigned _:3;
    };
} PORTA_SH;

// DCC stuff
// to calibrate transmitor
#define ONE_BIT_DUTY 56
#define ONE_BIT_PERIOD 115

#define ZERO_BIT_DUTY 98
#define ZERO_BIT_PERIOD 200

volatile __bit packet_ready; // whether we have a packet ready to send
__bit buffer; // 0: buffer0, 1:buffer1
//buffers for DCC packets. There are 2: one for current transmission, one for the next
#define BUFFER_LENGTH 6
unsigned char buffer0[BUFFER_LENGTH] = {0xFF, 0xFE, 0b11100101, 0b01110010, 0b10100000, 0b00111111};
unsigned char buffer1[BUFFER_LENGTH] = {0xFF, 0xFE, 0,0,0,0};
unsigned char *next_buffer; // allows the system to point to the buffer that is ready for transmit
unsigned char MASKS[8]; // this contains the bit masks, rather than shifting each cycle. This is because there is no hardware support for multiple shifts, and it takes precious cycles.

__bit change; //XXX

void __interrupt() ISR()
{
    static unsigned char *current_buffer = buffer0;
    static unsigned char index_bit = 0;
    static unsigned char index_byte = 0;
    static unsigned char current_bit=1; // used to speed up the setting of the pwm period

    static unsigned short int count = 2000; // XXX 

    //millisecond interrupt for LED control
    if (TIMER0_INTERRUPT_FLAG) // if the timer0 interrupt flag was set (timer0 triggered)
    {
        // DAT_LED = ON;
        TIMER0_INTERRUPT_FLAG = CLEAR; // clear interrupt flag since we are dealing with it
        TIMER0_COUNTER = TIMER0_INITIAL + 2; // reset counter, but also add 2 since it takes 2 clock cycles to get going
        // DAT_LED = OFF;

        // count--;
        // if (!count)
        // {
        //     // DAT_LED = ON;
        //     count = 100;
        //     change = TRUE;
        //     // DAT_LED = OFF;

        // }
    }

    //connected to the PWM
    if (TIMER2_INTERRUPT_FLAG)
    {   
        // DAT_LED = ON;
        TIMER2_INTERRUPT_FLAG = OFF;
        //set pwm for this cycle
        if (current_bit)
        {
            PWM_PERIOD = ONE_BIT_PERIOD;
        }
        else
        {
            PWM_PERIOD = ZERO_BIT_PERIOD;
        }

        // get next bit from buffer. It is counted using two chars, because shifting (index>>3) doesn't have hardware support, so takes precious cycles
        index_bit++;
        if (index_bit > 7)
        {
            index_bit = 0;
            index_byte++;
            if (index_byte > 1)
            {
                DAT_LED = ON;
                index_byte = 0;
                if (packet_ready) //  a new packet is ready to transmit
                {
                    // DAT_LED = ON;
                    current_buffer = next_buffer;
                    packet_ready = FALSE;
                    DAT_LED = OFF;
                }
                DAT_LED = OFF;
            }
        }

        current_bit = current_buffer[index_byte] & (MASKS[7-index_bit]);


        if (current_bit) // if next bit is a one
        {
            PWM_DUTYCYCLE_MSB = ONE_BIT_DUTY; // the bit of the next cycle will be a one
        }
        else // if its a zero
        {
            PWM_DUTYCYCLE_MSB = ZERO_BIT_DUTY; // the bit of the next cycle will be a zero

        }
    }
}

void main()
{
    // OSCCON = 0b01110001; // 8MHz clock

    DAT_LED_TYPE = DIGITAL;
    DAT_LED_PIN = OUTPUT;
    PORTA_SH.DAT_LED = ON;

    DCC1_PIN = OUTPUT;
    DCC2_PIN = OUTPUT;

    // Set up timer0
    // calculate intial for accurate timing $ inital = TimerMax-((Delay*Fosc)/(Prescaler*4))
    TIMER0_COUNTER = TIMER0_INITIAL; // set counter
    TIMER0_CLOCK_SCOURCE = INTERNAL; // internal clock
    PRESCALER = 0; // enable prescaler for Timer0
    PS2=0; PS1=1; PS0=0; // Set prescaler to 1:8
    TIMER0_INTERRUPT = ON; // enable timer0 interrupts

    //setup PWM and timer 2 for data out
    TIMER2_CONTROL = (ON<<TIMER2_ON) | (PRESCALE_1<<TIMER_CLOCK_PRESCALE); // Timer 2 register
    PWM_PERIOD = ONE_BIT_PERIOD;
    PWM_DUTYCYCLE_MSB = ONE_BIT_DUTY; //defaut to sending a DCC-1 bit
    PWM_CONTROL = 0x03; // 4 instruction deadband
    ECCP_CONTROL = (HALF_BRIDGE<<PWM_MODE) | (ACTIVE_HIGH_ACTIVE_HIGH<<PWM_OUTPUT); //PWM register set
    PERIPHAL_INTERRUPT = ON;
    TIMER2_INTERRUPT = ON;

    //fill bit masks
    for (char i=0; i<8; i++)
    {
        MASKS[i] = 1<<i;
    }

    // packet_ready = TRUE;

    //turn on interrupts
    GLOBAL_INTERRUPTS = ON;

    while (1)
    {
        if (change)
        {
            if (!packet_ready)
            {
                if (buffer == 0) // if buffer0 is currently being transmitted
                {
                    // DAT_LED = ON;
                    next_buffer = buffer1; // make buffer1 the next buffer


                    packet_ready = TRUE;
                    buffer = 1;
                    // DAT_LED = OFF;
                }
                else if (buffer == 1) // if buffer1 is currently being transmitted
                {
                    next_buffer = buffer0;


                    packet_ready = TRUE;
                    buffer = 0;
                }
                change = FALSE;
            }
        }
    }
}

