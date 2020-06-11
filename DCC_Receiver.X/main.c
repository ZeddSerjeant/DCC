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

union reading // 16 BIT representation
{
    // arranged this way for memory locations to match
    struct
    {
        unsigned char low;
        unsigned char high;
    };
    
    unsigned short int byte; //reading1_array[1] # reading1_array[0]
} mem;
// Save memory!

// DCC stuff
// to calibrate receiver (microseconds)
#define ONE_DURATION_MIN 104
#define ONE_DURATION_MAX 128

#define ZERO_DURATION_MIN 180
#define ZERO_DURATION_MAX 20000

#define REQUIRED_PREAMBLE 14
#define PACKET_LENGTH 44

volatile __bit packet_ready; // whether we have a packet ready to be processed
__bit buffer; // 0: buffer0, 1:buffer1
//buffers for DCC packets. There are 2: one for current transmission, one for the next
#define BUFFER_LENGTH 6
unsigned char buffer0[BUFFER_LENGTH];
unsigned char buffer1[BUFFER_LENGTH];
unsigned char *next_buffer; // allows the system to point to the buffer that is ready for transmit
unsigned char MASKS[8]; // this contains the bit masks, rather than shifting each cycle. This is because there is no hardware support for multiple shifts, and it takes precious cycles.


//button
__bit button_state; // default unpressed

__bit TEST_LED;

void __interrupt() ISR()
{
    // initialize high to avoid post-programming/turning on crap
    static unsigned char button_count = 255; // for debounce. I'm using polling, not interrupts, due this: http://www.ganssle.com/debouncing.htm
    static __bit button_change; // so holding the button doesn't break things

    static __bit search_for_preamble;
    static unsigned char preamble_count = 0;
    static unsigned char *current_buffer = buffer0;
    static unsigned char buffer_index = 0;
    static unsigned char bit_index = 0;
    static unsigned char total_index = 0; // these are split for speed reasons
    static unsigned char current_bit = 0;

    static unsigned short int last_time=0;
    unsigned short int time=0; // to record the time between EDGE (INTF) interrupts, to distinguish bits

    if (EDGE_INTERRUPT_FLAG) // if an external edge interrupt is triggered
    {
        DAT_LED = ON;
        // interrim = TIMER1_L;
        // time = TIMER1_H;
        mem.low = TIMER1_L;
        mem.high = TIMER1_H;

        EDGE_INTERRUPT_FLAG = CLEAR;

        // interrim = ((unsigned short int)time<<8 | interrim);


        
        // DAT_LED = ON;
        time = mem.byte - last_time;
        last_time = mem.byte;

        if (time < ONE_DURATION_MIN) // glitch
        {
            // DAT_LED = OFF;
            search_for_preamble = TRUE; // dump packet
            preamble_count = 0;
            // DAT_LED = ON;
        }
        else if (time < ONE_DURATION_MAX) // its a one
        {
            if (search_for_preamble)
            {
                preamble_count++;
            }
            current_bit = 1;
        }
        else if (time < ZERO_DURATION_MIN) // a one combined with a zero, we are triggering on the incorrect edge
        {
            // DAT_LED = OFF;
            // EDGE_DIRECTION = ~EDGE_DIRECTION; // change the edge trigger
            search_for_preamble = TRUE;
            preamble_count = 0;
            // DAT_LED = ON;
        }
        else if (time < ZERO_DURATION_MAX) // its a zero!
        {
            if (search_for_preamble)
            {
                if (preamble_count >= REQUIRED_PREAMBLE) //  a valid packet beginning
                {
                    search_for_preamble = FALSE;
                    // DAT_LED = OFF;
                    // current_buffer = next_buffer;
                    total_index = 0;
                    buffer_index = 0;
                    bit_index = 0;
                    // DAT_LED = ON;
                }
                preamble_count = 0;
            }
            current_bit = 0;
        }
        else // massive glitch
        {
            // search_for_preamble = TRUE;
            // preamble_count = 0;
        }

        if (!search_for_preamble)
        {
            if (current_bit) // its a one, so make it so
            {
                current_buffer[buffer_index] = current_buffer[buffer_index] | (MASKS[7-bit_index]);
            }
            // if it is not a 1, its a zero, which I means I just skip writing to it
            total_index++;
            if (total_index == 8) // we have filled a buffer
            {
                // DAT_LED = OFF;
                packet_ready = TRUE;
                search_for_preamble = TRUE;
                // EDGE_INTERRUPT = OFF; // we don't know if can fill another buffer, let mainline deal with that
                // DAT_LED = ON;
            }
            bit_index++;
            if (bit_index > 7)
            {
                bit_index = 0;
                buffer_index++;
                
            }
            
        }


        DAT_LED = OFF;
    }

    //millisecond interrupt for LED control
    if (TIMER0_INTERRUPT_FLAG) // if the timer0 interrupt flag was set (timer0 triggered)
    {
        // DAT_LED = ON;
        TIMER0_INTERRUPT_FLAG = CLEAR; // clear interrupt flag since we are dealing with it
        TIMER0_COUNTER = TIMER0_INITIAL + 2; // reset counter, but also add 2 since it takes 2 clock cycles to get going

        if (BUTTON && button_count==0)
        {
            button_count = 30;
        }
        
        
        if (button_count > 1)
        {
            button_count--;
        }
        else if (button_count == 1)
        {
            if (BUTTON)
            {
                if (button_change)
                {
                    button_change = 0;
                    button_count = 255;
                    button_state = ~button_state;
                }               
            }
            else
            {
                button_change = 1;
                button_count = 0;
            }
        }
        // DAT_LED = OFF;
    }
}

void main()
{
    // OSCCON = 0b01110001; // 8MHz clock
    static unsigned char *current_buffer;

    DAT_LED_TYPE = DIGITAL;
    DAT_LED_PIN = OUTPUT;
    BUTTON_PIN = INPUT;
    // PORTA_SH.DAT_LED = ON;

    PWM_PIN = OUTPUT;

    // Set up timer0
    // calculate intial for accurate timing $ inital = TimerMax-((Delay*Fosc)/(Prescaler*4))
    TIMER0_COUNTER = TIMER0_INITIAL; // set counter
    TIMER0_CLOCK_SCOURCE = INTERNAL; // internal clock
    PRESCALER = 0; // enable prescaler for Timer0
    PS2=0; PS1=1; PS0=0; // Set prescaler to 1:8
    TIMER0_INTERRUPT = ON; // enable timer0 interrupts

    //set up timer1
    TIMER1_H = 0x00; TIMER1_L = 0x00;

    //setup PWM and timer 2 for data out
    TIMER2_CONTROL = (ON<<TIMER2_ON) | (PRESCALE_16<<TIMER_CLOCK_PRESCALE); // Timer 2 register
    PWM_PERIOD = 255;
    PWM_DUTYCYCLE_MSB = 30; //defaut to sending a DCC-1 bit
    ECCP_CONTROL = (SINGLE_OUTPUT<<PWM_MODE) | (ACTIVE_HIGH_ACTIVE_HIGH<<PWM_OUTPUT); //PWM register set


    //fill bit masks
    for (char i=0; i<8; i++)
    {
        MASKS[i] = 1<<i;
    }

    // // packet_ready = TRUE;

    //enable INT (edge triggers)
    DATA_TYPE = DIGITAL;
    DATA_PIN = INPUT;
    EDGE_DIRECTION = FALLING; // Trigger on rising edges
    EDGE_INTERRUPT = ON; //enable the interrupt 




    //turn on interrupts
    GLOBAL_INTERRUPTS = ON;

    // enable counting
    TIMER1 = ON;

    while (1)
    {
        if (button_state)
        {
            // DAT_LED = ON;
            PWM_DUTYCYCLE_MSB = 100;
        }
        else
        {
            // DAT_LED = OFF;
            PWM_DUTYCYCLE_MSB = 0;
        }

        // if (packet_ready) // a packet has been received
        // {
        //     // allow the system to buffer another packet
        //     if (buffer) // current filled buffer is buffer1
        //     {
        //         current_buffer = buffer1;
        //         next_buffer = buffer0;
        //         buffer = 0;
        //     }
        //     else // current filled buffer is buffer0
        //     {
        //         current_buffer = buffer0;
        //         next_buffer = buffer1;
        //         buffer = 1;
        //     }

        //     EDGE_INTERRUPT = ON;

        //     if (current_buffer[0] == 0x00)
        //     {
        //         DAT_LED = OFF;
        //     }
        //     else if (current_buffer[0] == 0x7F)
        //     {
        //         DAT_LED = ON;
        //     }
        // }

        // PORTA = PORTA_SH.byte;

        // if (DATA)
        // {
        //     DAT_LED = ON;
        // }
        // else
        // {
        //     DAT_LED = OFF;
        // }
    }
}

