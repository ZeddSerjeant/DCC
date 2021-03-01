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

// period of flashing LED [ms]
#define LED_PERIOD 255
unsigned short int led_duty_cycle_counter = 0;
unsigned short int led_duty_cycle = 0; // Duty cycle of LED as on_time[ms]
unsigned char led_state;
__bit reset; // to indicate if devices downstream should reset

//button
unsigned char button_state = 0; // default unpressed


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
//0b11100101
unsigned char buffer0[BUFFER_LENGTH] = {0xFF, 0xFE, 0,0,0,0};
unsigned char buffer1[BUFFER_LENGTH] = {0xFF, 0xFE, 0,0,0,0};
unsigned char *next_buffer; // allows the system to point to the buffer that is ready for transmit
unsigned char MASKS[8]; // this contains the bit masks, rather than shifting each cycle. 
//This is because there is no hardware support for multiple shifts, and it takes precious cycles.


void __interrupt() ISR()
{
    static unsigned char *current_buffer = buffer0;
    static unsigned char index_bit = 0;
    static unsigned char index_byte = 0;
    static unsigned char current_bit=1; // used to speed up the setting of the pwm period

    static unsigned char button_count = 255; // for debounce. I'm using polling, not interrupts, due this: http://www.ganssle.com/debouncing.htm
    static __bit button_change; // so holding the button doesn't break things

    //connected to the PWM, outputs datastream
    if (TIMER2_INTERRUPT_FLAG)
    {   
        TIMER2_INTERRUPT_FLAG = CLEAR;

        //set pwm period for this cycle
        if (current_bit)
        {
            PWM_PERIOD = ONE_BIT_PERIOD;
        }
        else
        {
            PWM_PERIOD = ZERO_BIT_PERIOD;
            TIMER0_INTERRUPT = ON; // Turn this on, since a zero is long enough to allow other interrupts
        }

        // get next bit from buffer. It is counted using two chars, because shifting (index>>3) doesn't have hardware support, so takes precious cycles
        index_bit++;
        if (index_bit > 7)
        {
            index_bit = 0;
            index_byte++;
            if (index_byte == BUFFER_LENGTH) // check to see if we have output an entire buffer
            {
                PORTA = 0x4 | led_state; // indicate the packet, without breaking other LEDS
                index_byte = 0;
                if (packet_ready) // if there is a new packet that is ready to transmit, do that. Otherwise, retransmit this one
                {
                    current_buffer = next_buffer;
                    packet_ready = FALSE;
                }
                PORTA = led_state; // turn off IND_LED without affecting other LEDS
            }
        }

        current_bit = current_buffer[index_byte] & (MASKS[7-index_bit]); // fetch current bit, using pregenerated mask for speed

        if (current_bit) // if next bit is a one
        {
            PWM_DUTYCYCLE_MSB = ONE_BIT_DUTY; // the bit of the next cycle will be a one
        }
        else // if its a zero
        {
            PWM_DUTYCYCLE_MSB = ZERO_BIT_DUTY; // the bit of the next cycle will be a zero

        }
    }

    //millisecond interrupt for LED control
    else if (TIMER0_INTERRUPT_FLAG) // if the timer0 interrupt flag was set (timer0 triggered)
    {
        TIMER0_INTERRUPT_FLAG = CLEAR; // clear interrupt flag since we are dealing with it
        TIMER0_COUNTER = TIMER0_INITIAL + 2; // reset counter, but also add 2 since it takes 2 clock cycles to get going

        led_duty_cycle_counter++; // increment the led counter
        
        if (led_duty_cycle_counter >= led_duty_cycle) // toggle state
        {
            if (led_duty_cycle_counter >= LED_PERIOD) // restart cycle
            {
                led_duty_cycle_counter -= LED_PERIOD; //reset led counter safely
                // led_state = ON; // we are in the ON part of the duty cycle
            }
            else
            {
                led_state = OFF;
            }
        }
        else
        {
            led_state = ON; // within On part of duty cycle
        }

        //button press
        // debounce
        if (BUTTON && button_count==0)
        {
            button_count = 30;
        }
        else if (button_count > 1)
        {
            button_count--;

            if (reset) //flash reset led
            {
                CLK_LED = ON;
            }
        }
        else if (button_count == 1)
        {
            if (BUTTON)
            {
                if (button_change) // if the button was definitely pressed
                {
                    button_change = 0;
                    button_count = 255;
                    button_state++;
                    button_state = button_state%3; // three states

                    reset = TRUE; // upon a press, send a reset signal. button_state can be used to detect if this has occured

                }               
            }
            else
            {
                reset = FALSE;
                button_change = 1;
                button_count = 0;
            }
        }
        TIMER0_INTERRUPT = OFF; // turn off until the pwm turns it back on when it has time

    }
}

void main()
{
    unsigned char target_address = 0b00000101; // the device we will be talking to
    unsigned char checksum;

    // initialize pins
    DAT_LED_TYPE = DIGITAL;
    DAT_LED_PIN = OUTPUT;
    CLK_LED_TYPE = DIGITAL;
    CLK_LED_PIN = OUTPUT;
    IND_LED_TYPE = DIGITAL;
    IND_LED_PIN = OUTPUT;

    DCC1_PIN = OUTPUT;
    DCC2_PIN = OUTPUT;

    // Set up timer0
    // calculate intial for accurate timing $ inital = TimerMax-((Delay*Fosc)/(Prescaler*4))
    TIMER0_COUNTER = TIMER0_INITIAL; // set counter
    TIMER0_CLOCK_SCOURCE = INTERNAL; // internal clock
    PRESCALER = 0; // enable prescaler for Timer0
    PS2=0; PS1=1; PS0=0; // Set prescaler to 1:8
    // TIMER0_INTERRUPT = ON; // enable timer0 interrupts

    //setup PWM and timer 2 for data out
    TIMER2_CONTROL = (ON<<TIMER2_ON) | (PRESCALE_1<<TIMER_CLOCK_PRESCALE); // Timer 2 register
    PWM_PERIOD = ONE_BIT_PERIOD;
    PWM_DUTYCYCLE_MSB = ONE_BIT_DUTY; //defaut to sending a DCC-1 bit
    PWM_CONTROL = 0x03; // 4 instruction deadband
    ECCP_CONTROL = (HALF_BRIDGE<<PWM_MODE) | (ACTIVE_HIGH_ACTIVE_HIGH<<PWM_OUTPUT); //PWM register set
    PERIPHAL_INTERRUPT = ON;
    TIMER2_INTERRUPT = ON;

    //fill bit masks (pregenerated for speed)
    for (char i=0; i<8; i++)
    {
        MASKS[i] = 1<<i;
    }

    //Set up ADC
    // POT_PIN = INPUT;
    // POT_TYPE = ANALOG;
    ADC_VOLTAGE_REFERENCE = INTERNAL;
    ADC_CHANNEL2 = 1; ADC_CHANNEL1 = 1; ADC_CHANNEL0 = 1; // Set the channel to AN7 (where the POT is)
    ADC_CLOCK_SOURCE2 = 0; ADC_CLOCK_SOURCE1 = 0; ADC_CLOCK_SOURCE0 = 1; // Set the clock rate of the ADC
    ADC_OUTPUT_FORMAT = LEFT; // right Shifted ADC_RESULT_HIGH contains the first 2 bits
    // ADC_INTERRUPT = OFF; // by default these aren't necessary
    ADC_ON = ON; // turn it on

    //turn on interrupts
    GLOBAL_INTERRUPTS = ON;

    while (1)
    {
        if (!packet_ready) // if a new packet hasn't already been generated (both buffers are thus full/being processed)
        {
            if (buffer == 0) // if buffer0 is currently being transmitted
            {
                next_buffer = buffer1; // make buffer1 the next buffer
                buffer = 1;
            }
            else if (buffer == 1) // if buffer1 is currently being transmitted
            {
                next_buffer = buffer0;
                buffer = 0;
            }

            if (button_state == 0) // normal transmit mode
            {
                //fetch data
                ADC_GODONE = ON;
                while(ADC_GODONE);

                //output this value as an LED
                led_duty_cycle = ADC_RESULT_HIGH;

                // calculate checksum byte (xor of address and data)
                checksum = target_address ^ ADC_RESULT_HIGH;

                //fill buffer
                next_buffer[0] = 0xFF;
                next_buffer[1] = 0xFE; // preamble
                next_buffer[2] = target_address;
                next_buffer[3] = ADC_RESULT_HIGH>>1; // shifted because the protocol demands a zero at the front
                next_buffer[4] = (ADC_RESULT_HIGH<<7) | (checksum>>2); // contains last bit of data, a zero, then 6 bits of checksum
                next_buffer[5] = (checksum<<6) | 0x3F; // contains last 2 bits of checksum, 
                //then the packet end bit (one) then a stream of ones to link into the next preamble
            }
            else if (button_state == 1) // blank state for demo
            {
                for (int i=0; i<BUFFER_LENGTH; i++)
                {
                    next_buffer[i] = 0; // corrupt buffer for demonstration
                }

                led_duty_cycle = 0;
            }
            else if (button_state == 2) // output data to different address
            {
                ADC_GODONE = ON;
                while(ADC_GODONE);

                //output this value as an LED for testing
                led_duty_cycle = ADC_RESULT_HIGH;

                // calculate checksum byte (xor of address and data)
                checksum = target_address ^ ADC_RESULT_HIGH;

                next_buffer[0] = 0xFF;
                next_buffer[1] = 0xFE; // preamble
                next_buffer[2] = 0b00000001; // different address
                next_buffer[3] = ADC_RESULT_HIGH>>1; // shifted because the protocol demands a zero at the front
                next_buffer[4] = (ADC_RESULT_HIGH<<7) | (checksum>>2); // contains last bit of data, a zero, then 6 bits of checksum
                next_buffer[5] = (checksum<<6) | 0x3F; // contains last 2 bits of checksum, 
                //then the packet end bit (one) then a stream of ones to link into the next preamble
            }
            packet_ready = TRUE; // let transmit know there is a packet ready   
        }
    }
}

