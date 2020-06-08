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

#define PREAMBLE_LENGTH (unsigned char)12
__bit packet_ready; // whether we have a packet ready to send
//buffers for DCC packets. There are 2: one for current transmission, one for the next
#define BUFFER_LENGTH 8
unsigned char buffer1[BUFFER_LENGTH] = {1,1,1,0,1,0,0,1};
// unsigned char buffer2[4];


void __interrupt() ISR()
{
    static unsigned char preamble_countdown = PREAMBLE_LENGTH;
    static unsigned char *current_buffer = buffer1;
    static unsigned char index = 0;
    static unsigned char current_bit; // used to speed up the setting of the pwm period

    //millisecond interrupt for LED control
    if (TIMER0_INTERRUPT_FLAG) // if the timer0 interrupt flag was set (timer0 triggered)
    {
        // DAT_LED = ON;
        TIMER0_INTERRUPT_FLAG = CLEAR; // clear interrupt flag since we are dealing with it
        TIMER0_COUNTER = TIMER0_INITIAL + 2; // reset counter, but also add 2 since it takes 2 clock cycles to get going
        // DAT_LED = OFF;
    }

    //connected to the PWM
    if (TIMER2_INTERRUPT_FLAG)
    {   
        //set pwm for this cycle
        if (current_bit)
        {
            PWM_PERIOD = ONE_BIT_PERIOD;
        }
        else
        {
            PWM_PERIOD = ZERO_BIT_PERIOD;
        }

        // get next bit from buffer
        index++;
        if (index >= BUFFER_LENGTH)
        {
            DAT_LED = ON;
            index = 0;
            DAT_LED = OFF;
        }

        current_bit = buffer1[index];
       
        if (current_bit) // if next bit is a one
        {
             PWM_DUTYCYCLE_MSB = ONE_BIT_DUTY; // the bit of the next cycle will be a one
        }
        else // if its a zero
        {
            PWM_DUTYCYCLE_MSB = ZERO_BIT_DUTY; // the bit of the next cycle will be a zero
        }


        TIMER2_INTERRUPT_FLAG = OFF;
        // DAT_LED = OFF;
        
    }
}

void main()
{
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

    //turn on interrupts
    GLOBAL_INTERRUPTS = ON;

    while (1)
    {
        // DCC1 = ON;
        // DCC2 = ON;

        // PORTA = PORTA_SH.byte;
        // DAT_LED = OFF;
    }
}

