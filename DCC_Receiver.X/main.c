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
#define ONE_DURATION_MAX 132

#define ZERO_DURATION_MIN 180
#define ZERO_DURATION_MAX 20000

#define REQUIRED_PREAMBLE 14
#define PACKET_LENGTH 44

volatile __bit packet_ready; // whether we have a packet ready to be processed
volatile __bit packet_found;
volatile __bit error;
//buffers for DCC packets. There are 2: one for current transmission, one for the next
#define BUFFER_LENGTH 6
unsigned char buffer[1];
unsigned char buffer_index=0;
// unsigned char buffer1[BUFFER_LENGTH];
unsigned char bit_buffer[16]; // store bits before they are combined into a byte, for speed
unsigned char bit_index = 0;
__bit byte; // indicates whether we are in the first 8bits of the bit buffer, or last 8 bits.  This is so the check doesn't have to happen in the interrupt
__bit byte_ready;
unsigned char *next_buffer; // allows the system to point to the buffer that is ready for transmit
unsigned char MASKS[8]; // this contains the bit masks, rather than shifting each cycle. This is because there is no hardware support for multiple shifts, and it takes precious cycles.
unsigned short int time=0; // to record the time between EDGE (INTF) interrupts, to distinguish bits

//button
__bit button_state; // default unpressed

__bit TEST_LED;

void __interrupt() ISR()
{
    // initialize high to avoid post-programming/turning on crap
    static unsigned char button_count = 255; // for debounce. I'm using polling, not interrupts, due this: http://www.ganssle.com/debouncing.htm
    static __bit button_change; // so holding the button doesn't break things

    static unsigned char preamble_count = 0;
    // static unsigned char *current_buffer = bit_buffer0;
    static unsigned char current_bit = 0;

    static unsigned short int last_time=0;

    static __bit test = 0;
    
    // if (TIMER1_INTERRUPT_FLAG)
    // {
    //     TIMER1_INTERRUPT_FLAG = OFF;

    //     test = 1;
    // }

    if (EDGE_INTERRUPT_FLAG) // if an external edge interrupt is triggered
    {
        PORTA = 0x3;
        mem.low = TIMER1_L;
        mem.high = TIMER1_H;

        EDGE_INTERRUPT_FLAG = CLEAR;

        
        time = mem.byte - last_time;
        last_time = mem.byte;

        // for now, I'll leave this out, as these glitches can be removed by the decoder, rather than this code, the 'capturer'
        // really no reason to do this here, as the system waits for the next preamble regardless, and we have no idea where that will that will occur.
        // if (time < ONE_DURATION_MIN) // glitch
        // {
        //     // DAT_LED = OFF;
        //     packet_found = FALSE;
        //     preamble_count = 0;
        //     // DAT_LED = ON;
        // }
        if (time < ONE_DURATION_MAX) // its a one
        {
            // PORTA = 0x1;
            preamble_count++;
            current_bit = 1;
            // PORTA = 0x3;
        }
        else if (time < ZERO_DURATION_MIN) // a one combined with a zero, we are triggering on the incorrect edge
        {
            
            // EDGE_DIRECTION = ~EDGE_DIRECTION; // change the edge trigger
            error = TRUE;
            packet_found = FALSE;
            preamble_count = 0;
            
        }
        else if (time < 300) // its a zero! (temp shorter time due to bugs...)
        {
            // NXT_LED = OFF;
            if (preamble_count >= REQUIRED_PREAMBLE) //  a valid packet beginning
            {                
                packet_found = TRUE;
                buffer_index = 0;
                bit_index = 0;
            }
            preamble_count = 0;
            current_bit = 0;
            // NXT_LED = ON;
            

            //rest to prevent overflow; happens in here because this is the longest timeframe within this interrupt
            // last_time = 0;
            // TIMER1_L = 0;
            // TIMER1_H = 0;

            
        }
        // else // massive glitch, or the tranmission has fucked up. For something like this to occur client side, multiple bits have to be missed, and it can be cleaned up in the encoder anyway
        // {
            // if (test)
            // {
            //     NXT_LED = OFF;
            //     test = 0;
            //     NXT_LED = ON;

            // }
        // }

        
        bit_buffer[bit_index] = current_bit;
        bit_index++;
        bit_index = bit_index%16;

        PORTA = 0x2;
    }

    //millisecond interrupt for LED control
    // if (TIMER0_INTERRUPT_FLAG) // if the timer0 interrupt flag was set (timer0 triggered)
    // {
    //     // DAT_LED = ON;
    //     TIMER0_INTERRUPT_FLAG = CLEAR; // clear interrupt flag since we are dealing with it
    //     TIMER0_COUNTER = TIMER0_INITIAL + 2; // reset counter, but also add 2 since it takes 2 clock cycles to get going

    //     if (BUTTON && button_count==0)
    //     {
    //         button_count = 30;
    //     }
        
        
    //     if (button_count > 1)
    //     {
    //         button_count--;
    //     }
    //     else if (button_count == 1)
    //     {
    //         if (BUTTON)
    //         {
    //             if (button_change)
    //             {
    //                 button_change = 0;
    //                 button_count = 255;
    //                 button_state = ~button_state;
    //             }               
    //         }
    //         else
    //         {
    //             button_change = 1;
    //             button_count = 0;
    //         }
    //     }
    //     // DAT_LED = OFF;
    // }
}

void main()
{
    OSCCON = 0b01110001; // 8MHz clock

    DAT_LED_TYPE = DIGITAL;
    DAT_LED_PIN = OUTPUT;
    CLK_LED_TYPE = DIGITAL;
    CLK_LED_PIN = OUTPUT;
    NXT_LED_PIN = OUTPUT;
    NXT_LED_TYPE = DIGITAL;

    BUTTON_PIN = INPUT;
    // PORTA_SH.DAT_LED = ON;

    PWM_PIN = OUTPUT;

    // Set up timer0
    // calculate intial for accurate timing $ inital = TimerMax-((Delay*Fosc)/(Prescaler*4))
    TIMER0_COUNTER = TIMER0_INITIAL; // set counter
    TIMER0_CLOCK_SCOURCE = INTERNAL; // internal clock
    PRESCALER = 0; // enable prescaler for Timer0
    PS2=0; PS1=1; PS0=0; // Set prescaler to 1:8
    // TIMER0_INTERRUPT = ON; // enable timer0 interrupts

   
    //setup PWM and timer 2 for bulb
    // TIMER2_CONTROL = (ON<<TIMER2_ON) | (PRESCALE_16<<TIMER_CLOCK_PRESCALE); // Timer 2 register
    // PWM_PERIOD = 255;
    // PWM_DUTYCYCLE_MSB = 30; //defaut to sending a DCC-1 bit
    // ECCP_CONTROL = (SINGLE_OUTPUT<<PWM_MODE) | (ACTIVE_HIGH_ACTIVE_HIGH<<PWM_OUTPUT); //PWM register set


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


    //set up timer1
    TIMER1_H = 0x00; TIMER1_L = 0x00;
    T1CKPS1 = 0; T1CKPS0 = 1; //TIMER1 prescaler
    TIMER1 = ON; // turn the timer1 on, with a 1:2 prescale so with an 8MHz clock, it counts microseconds
    // TIMER1_INTERRUPT = ON;
    // PERIPHAL_INTERRUPT = ON;


    //turn on interrupts
    GLOBAL_INTERRUPTS = ON;

    
    while (1)
    {
        if (packet_found) // a packet will begin being written into bit_buffer
        {
            // DAT_LED = ON;
            buffer[buffer_index] = 0; // zero out the current byte
            if (byte == 0) // first byte of buffer is being recorded
            {
                while (1) // loop for this, for speed
                {
                    if (bit_index > 7) // the first byte is full, we can move it
                    {
                        // DAT_LED = OFF;
                        for (int i=0; i<8; i++)
                        {
                            if (bit_buffer[i]) // if its a one, make it so, otherwise skip
                            {
                                buffer[buffer_index] |= (MASKS[7-i]);
                            }
                        }
                        // DAT_LED = ON;
                        break;
                    }
                }
                buffer_index++;
                byte = 1;
            }
            else // second byte of the buffer is being recorded
            {
                while (1) // loop for this, for speed
                {
                    if (bit_index < 7) // the second byte is full, we can move it
                    {
                        // DAT_LED = OFF;
                        for (int i=8; i<16; i++)
                        {
                            if (bit_buffer[i]) // if its a one, make it so, otherwise skip
                            {
                                buffer[buffer_index] |= (MASKS[7-i]);
                            }
                        }
                        // DAT_LED = ON;
                        break;
                    }
                }
                buffer_index++;
                byte = 0;
            }
        }

        if (buffer_index == 1) // buffer full, decode
        {
            if (buffer[0] == 0x7F)
            {
                CLK_LED = OFF;
            }
            else
            {
                CLK_LED = ON;
            }

            buffer_index = 0;
            packet_found = FALSE;
        }

        // //process button
        // if (button_state)
        // {
        //     // DAT_LED = ON;
        //     PWM_DUTYCYCLE_MSB = 100;
        // }
        // else
        // {
        //     // DAT_LED = OFF;
        //     PWM_DUTYCYCLE_MSB = 0;
        // }


        // if (current_bit) // its a one, so make it so
        // {
        //     current_buffer[buffer_index] = current_buffer[buffer_index] | (MASKS[7-bit_index]);
        // }
        // if it is not a 1, its a zero, which I means I just skip writing to it

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

