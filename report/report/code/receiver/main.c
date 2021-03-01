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

#define ADDRESS 0b00000101

//led stuff
#define LED_TIME 2
unsigned char packet_led = 0;//indicate to the user when a packet has been detected
unsigned char update_led = 0;//indicate to the user when a packet is for this device, and that this device has acted upon the information 
                             //(bulb brightness adjusted)
unsigned char error_led = 0; //indicate when a packet is dropped due to some error

// initialize high to avoid post-programming/turning noise
static unsigned char button_count = 100; // for debounce. I'm using polling, not interrupts, due this: http://www.ganssle.com/debouncing.htm
static __bit button_change; // so holding the button doesn't break things



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

union reading // 16 BIT representation, to save memory
{
    // arranged this way for memory locations to match
    struct
    {
        unsigned char low;
        unsigned char high;
    };
    
    unsigned short int byte; //reading1_array[1] # reading1_array[0]
} mem;

// DCC stuff
// to calibrate receiver (microseconds)
#define ONE_DURATION_MIN 104
#define ONE_DURATION_MAX 132

#define ZERO_DURATION_MIN 180
#define ZERO_DURATION_MAX 20000

#define REQUIRED_PREAMBLE 14
#define PACKET_LENGTH 44

volatile __bit packet_found;
volatile __bit error;
//buffers for DCC packets. There are 2: one for current transmission, one for the next
#define BUFFER_LENGTH 6
unsigned char buffer[BUFFER_LENGTH];
unsigned char buffer_index=0;
// unsigned char buffer1[BUFFER_LENGTH];
unsigned char bit_buffer[16]; // store bits before they are combined into a byte, for speed
unsigned char bit_index = 0;
__bit byte; // indicates whether we are in the first 8bits of the bit buffer, or last 8 bits.  This is so the check doesn't have to happen in the interrupt
__bit byte_ready;
unsigned char MASKS[8]; // this contains the bit masks, rather than shifting each cycle. This is because there is no hardware support for multiple shifts, 
//                          and it takes precious cycles.
unsigned short int time=0; // to record the time between EDGE (INTF) interrupts, to distinguish bits

//button
unsigned char button_state = 0; // default unpressed

void __interrupt() ISR()
{
    
    YEL_LED = ON;
    static unsigned char preamble_count = 0;
    // static unsigned char *current_buffer = bit_buffer0;
    static unsigned char current_bit = 0;

    static unsigned short int last_time=0; // time of last interrupt

    if (EDGE_INTERRUPT_FLAG) // if an external edge interrupt is triggered
    {
        //represent time as 16 bit
        mem.low = TIMER1_L;
        mem.high = TIMER1_H;

        EDGE_INTERRUPT_FLAG = CLEAR;

        
        time = mem.byte - last_time;
        last_time = mem.byte;

        
        if (time < ONE_DURATION_MIN) // glitch, so search for new preamble
        {
            packet_found = FALSE;
            preamble_count = 0;
            buffer_index = 0;
        }
        else if (time < ONE_DURATION_MAX) // its a one
        {
            preamble_count++;
            current_bit = 1;
        }
        else if (time < ZERO_DURATION_MIN) // a one combined with a zero, we are triggering on the incorrect edge
        {
            
            EDGE_DIRECTION = ~EDGE_DIRECTION; // change the edge trigger, since that solves this issue
            error = TRUE;
            packet_found = FALSE;
            preamble_count = 0;
            buffer_index = 0;
            
        }
        else if (time < 400) // its a zero! (temp shorter time due to bugs...)
        {
            if (preamble_count >= REQUIRED_PREAMBLE) //  a valid packet beginning
            {                
                packet_found = TRUE;
                buffer_index = 0;
                bit_index = 0;
            }
            preamble_count = 0;
            current_bit = 0;
        }
        else // massive glitch, or the tranmission has fucked up. For something like this to occur client side, multiple bits have to be missed
            //and it can be cleaned up in the encoder anyway
        {
            preamble_count = 0;
            packet_found = FALSE;
            buffer_index = 0;
        }

        
        bit_buffer[bit_index] = current_bit;
        bit_index++;
        bit_index = bit_index%16;
    }

    YEL_LED = OFF;
}

void main()
{
    unsigned char address, data, checksum;

    OSCCON = 0b01110001; // 8MHz clock (by default, can toggle to 4MHz)

    // set pins
    DAT_LED_TYPE = DIGITAL;
    DAT_LED_PIN = OUTPUT;
    CLK_LED_TYPE = DIGITAL;
    CLK_LED_PIN = OUTPUT;
    NXT_LED_PIN = OUTPUT;
    NXT_LED_TYPE = DIGITAL;
    YEL_LED_PIN = OUTPUT;
    YEL_LED_TYPE = DIGITAL;

    BUTTON_PIN = INPUT;

    PWM_PIN = OUTPUT;

   
    //setup PWM and timer 2 for bulb
    TIMER2_CONTROL = (ON<<TIMER2_ON) | (PRESCALE_16<<TIMER_CLOCK_PRESCALE); // Timer 2 register
    PWM_PERIOD = 255;
    PWM_DUTYCYCLE_MSB = 30;
    ECCP_CONTROL = (SINGLE_OUTPUT<<PWM_MODE) | (ACTIVE_HIGH_ACTIVE_HIGH<<PWM_OUTPUT); //PWM register set


    //fill bit masks. This is precomputed for performance
    for (char i=0; i<8; i++)
    {
        MASKS[i] = 1<<i;
    }

    //enable INT (edge triggers) for detecting the bitstream
    DATA_TYPE = DIGITAL;
    DATA_PIN = INPUT;
    EDGE_DIRECTION = FALLING; // Trigger on rising edges
    EDGE_INTERRUPT = ON; //enable the interrupt 


    //set up timer1 for timing the bits
    TIMER1_H = 0x00; TIMER1_L = 0x00;
    T1CKPS1 = 0; T1CKPS0 = 1; //TIMER1 prescaler
    TIMER1 = ON; // turn the timer1 on, with a 1:2 prescale so with an 8MHz clock, it counts microseconds
    // TIMER1_INTERRUPT = ON;
    // PERIPHAL_INTERRUPT = ON;


    //turn on interrupts
    GLOBAL_INTERRUPTS = ON;

    
    while (1)
    {   
        if (packet_found) // a packet will begin being written into bit_buffer (system found a preamble)
        {
            buffer[buffer_index] = 0; // clear the current byte
            if (byte == 0) // first byte of buffer is being recorded
            {
                while (1) // loop for this, for speed, by holding the instruction pointer hostage
                {
                    if (bit_index > 7) // the first byte is full, we can move it
                    {
                        for (int i=0; i<8; i++)
                        {
                            if (bit_buffer[i]) // if its a one, make it so, otherwise skip this bit (defaults to zero)
                            {
                                buffer[buffer_index] |= (MASKS[7-i]); // use the mask to set to 1
                            }
                        }
                        break; // leave loop
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
                        for (int i=8; i<16; i++)
                        {
                            if (bit_buffer[i]) // if its a one, make it so, otherwise skip
                            {
                                buffer[buffer_index] |= (MASKS[7-(i-8)]);
                            }
                        }
                        break;
                    }
                }
                buffer_index++;
                byte = 0;
            }
        }

        if (buffer_index == 4) // buffer full, begin decode of packet
        {
            packet_led = LED_TIME; // indicate that the packet has been found

            // buffer is rearanged to remove the structure imposed by DCC and the receiver optimisation
            address = (buffer[0]<<1) | (buffer[1]>>7);
            data = (buffer[1]<<2) | (buffer[2]>>6);
            checksum = (buffer[2]<<3) | (buffer[3]>>5);

            if ((address ^ data) == checksum) // as per DCC spec: is this a valid packet?
            {
                if (address == ADDRESS) //if this packet was addressed to this receiver
                {
                    update_led = LED_TIME; // indicate that a packet was aimed at this device

                    if (button_state == 1) // if the button has been pressed
                    {
                        PWM_DUTYCYCLE_MSB = 255; // put the output full on
                    }
                    else // if the user has not requested dimming, then allow the transmitter to control it
                    {
                        PWM_DUTYCYCLE_MSB = data;
                    }
                }
            }
            else // the packet was corrupted
            {
                error_led = LED_TIME; // indicate that the packet was corrupted
            }
            buffer_index = 0;
            packet_found = FALSE;
        }
        else //  no packet to decode, update other parts of system (this doesn't use a timer so the interrupt can be fast.)
        {
            // this doesn't impact anything, since the timing isn't crucial, the LEDs only indicate things to the user
            
            //LED TIMING
            if (packet_led) // if we need to display whether a packet has been received
            {
                packet_led--;
                PORTA_SH.DAT_LED = ON;
            }
            else
            {
                PORTA_SH.DAT_LED = OFF;
            }

            if (update_led) // if we need to display whether a packet has been received
            {
                update_led--;
                PORTA_SH.CLK_LED = ON;
            }
            else
            {
                PORTA_SH.CLK_LED = OFF;
            }

            if (error_led) // if we need to display whether a packet has been received
            {
                error_led--;
                NXT_LED = ON;
            }
            else
            {
                NXT_LED = OFF;
            }

            // process button input. This is safer as a polling exercise anyway
            // debounce
            if (BUTTON && button_count==0)
            {
                button_count = 5;
            }
            else if (button_count > 1)
            {
                button_count--;
            }
            else if (button_count == 1)
            {
                if (BUTTON)
                {
                    if (button_change) // if the button has actually been pressed, change state
                    {
                        button_change = 0;
                        button_count = 100;
                        button_state++;

                        button_state = button_state%3; //three states 

                        if (button_state == 0) // this state also forces a clockspeed update
                        {
                            OSCCON = 0b01110001; // 8MHz clock
                            T1CKPS1 = 0; T1CKPS0 = 1; //TIMER1 prescaler (1:2)
                        }
                        else if (button_state == 2) // clockspeed update
                        {
                            OSCCON = 0b01100001; // 4MHz clock
                            T1CKPS1 = 0; T1CKPS0 = 0; //TIMER1 prescaler (1:2)
                        }
                    }               
                }
                else
                {
                    button_change = 1;
                    button_count = 0;
                }
            }
        }

        PORTA = PORTA_SH.byte; // update LEDs
    }
}

