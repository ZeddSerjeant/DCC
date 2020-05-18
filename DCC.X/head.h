#include <xc.h> // include processor files - each processor file is guarded. 

enum FLAGS {OFF=0, OUTPUT=0, INTERNAL=0, CLEAR=0, DIGITAL=0, ANALOG=1, INPUT=1, ON=1, RIGHT=1};
#define GLOBAL_INTERRUPTS GIE

//Pins

#define DAT_LED RA0
#define DAT_LED_PIN TRISA0 
#define DAT_LED_TYPE ANS0
#define DAT_LED_BIT 0b000001

#define CLK_LED RA1
#define CLK_LED_PIN TRISA1
#define CLK_LED_TYPE ANS1
#define CLK_LED_BIT 0b000010

#define IND_LED RA2
#define IND_LED_PIN TRISA2 
#define IND_LED_TYPE ANS2
#define IND_LED_BIT 0b000100

#define PWM_MOTOR RC5
#define PWM_MOTOR_PIN TRISC5

#define MOTOR_READING RC1
#define MOTOR_READING_PIN TRISC1
#define MOTOR_READING_TYPE ANS5

// button
#define BUTTON RA3
#define BUTTON_PIN TRISA3 
#define BUTTON_INTERRUPT IOCA3
#define RA_INTERRUPT RAIE
#define BUTTON_INTERRUPT_FLAG RAIF

//POT
#define POT RC0
#define POT_PIN TRISC0
#define POT_TYPE ANS4

//TIMER0
#define PRESCALER PSA
#define TIMER0_COUNTER TMR0
#define TIMER0_CLOCK_SCOURCE T0CS
#define TIMER0_INTERRUPT T0IE
#define TIMER0_INTERRUPT_FLAG TMR0IF

//timer 1
#define TIMER1_H TMR1H
#define TIMER1_L TMR1L
#define TIMER1_INTERRUPT_FLAG TMR1IF
#define TIMER1_INTERRUPT TMR1IE
#define TIMER1 TMR1ON


//PWM
//parts are specified as an offset within a register
#define PWM_CONTROL CCP1CON
// #define PWM_MODE P1M
#define PWM_MODE (unsigned char)6
// #define PWM_OUTPUT CCP1M
#define PWM_OUTPUT (unsigned char)0
#define PWM_DUTYCYCLE_MSB CCPR1L
// #define PWM_DUTYCYCLE_LSB DC1B
#define PWM_DUTYCYCLE_LSB (unsigned char)4
#define PWM_PERIOD PR2

//timer 2 (needed for PWM)
#define TIMER2 TMR2
#define TIMER2_CONTROL T2CON
#define TIMER2_ON 2
#define TIMER2_INTERRUPT_FLAG TMR2IF
#define TIMER_CLOCK_PRESCALE (unsigned char)0
#define TIMER_CLOCK_POSTSCALE (unsigned char)3
#define PRESCALE_1 (unsigned char)0b00
#define PRESCALE_4 (unsigned char)0b01
#define PRESCALE_16 (unsigned char)0b10

#define PERIPHAL_INTERRUPT PEIE
#define TIMER2_INTERRUPT TMR2IE
 
// #define TIMER2_PRESCALER T2CKPS

#define ACTIVE_HIGH_ACTIVE_HIGH (unsigned char)0b1100
#define ACTIVE_LOW_ACTIVE_LOW (unsigned char)0b1111
#define SINGLE_OUTPUT (unsigned char)0b00

//ADC
#define ADC_VOLTAGE_REFERENCE VCFG
#define ADC_CLOCK_SOURCE2 ADCS2
#define ADC_CLOCK_SOURCE1 ADCS1
#define ADC_CLOCK_SOURCE0 ADCS0
#define ADC_CHANNEL2 CHS2
#define ADC_CHANNEL1 CHS1
#define ADC_CHANNEL0 CHS0
#define ADC_GODONE GO_DONE
#define ADC_OUTPUT_FORMAT ADFM 
#define ADC_RESULT_HIGH ADRESH
#define ADC_RESULT_LOW ADRESL
#define ADC_INTERRUPT ADIE
#define ADC_INTERRUPT_FLAG ADIF
#define ADC_ON ADON
#define ADC_PAUSE asm("NOP;");asm("NOP;");asm("NOP;");asm("NOP;");asm("NOP;");asm("NOP;")
