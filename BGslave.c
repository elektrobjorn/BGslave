// sevenseg.c
// The data is sent to the display with the bcd value to be displayed in bits 0-3, and the position of the digit (0-3) in bits 4-5.
// The decimal point is coded in bit 6. 
// Bit 7 is the load command that transfers the new data to the display.
// The I2C address is 0x33.

//
// Created: 2013-12-09
//			2015-05-05
//  Author: Bjorn
//
// Format of byte transmitted on TWI:
//	Bit 7:	Transfer buffered data to display
//	Bit 6:	Decimal point
//	Bit 5:	Digit position
//	Bit 4:	Digit position
//	Bit 3:	Display value	
//	Bit 2:	Display value
//	Bit 1:	Display value
//	Bit 0:	Display value
	
//ATmega8L
//********************************

//********************************
// FUSE BITS

// Fuse high
//  7      1  PC6 is RESET
//  6      1  Not WDT always on
//  5      0  SPI programming enabled
//  4      1  CKOPT
//  3      1  EEPROM not preserved during erase
//  2      1  Minimum boot section
//  1      1  -''-
//  0      1  Reset vector at 0

// Fuse low
//  7      1  BOD level
//  6      1  BOD disabled
//  5      1  Max start-up time
//  4      0  -''-
//  3      0  8 MHz internal RC
//  2      1  -''-
//  1      0  -''-
//  0      0  -''-

// LOCK BITS
// For maximum security, the whole lock byte can be programmed to 0.
// Lock byte programming does not affect operation under normal circumstances.

//********************************
// PORTS
//********************************

// B0		digit 0
// B1		digit 1
// B2		digit 2
// B3		digit 3			SPI MOSI
// B4		Battery LED green	SPI MISO
// B5		Battery LED red		SPI SCK
// B6		
// B7		Slave switch signal, active high		

// C0		Start switch
// C1		Up switch
// C2		Down switch
// C3		Set switch
// C4		SDA
// C5		SCL
// C6		RESET/		

// D0		segment g
// D1		segment f
// D2		segment e
// D3		segment d
// D4		segment c
// D5		segment b
// D6		segment a
// D7		segment dp

//***********************************************
// I2C connector
//
//	Pin 1	SDA
//	Pin 2	VCC (modified to slaveSwitchSignal)
//	Pin 3	SCL
//	Pin 4	VCC
//	Pin 5	GND
//	Pin 6	VCC
//**********************************************

//#define F_CPU	8000000UL			// CPU clock 8 MHz (moved to makefile)
#define SEGADDR	0x33			// 7-segment display address on I2C bus

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <ctype.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/io.h>

 
// 7-segment decoding table
// Bits [6:0] map to segments [a:g]
// Bit 7 maps to decimal point, and is handled
// outside of this table.
// Bit=0 turns on segment.


//for 7-segment displays mounted on backside of pcb
uint8_t seg[16]={
	0x81,		// 0
	0xF3,		// 1
	0x49,		// 2
	0x61,		// 3
	0x33,		// 4
	0x25,		// 5
	0x05,		// 6
	0xF1,		// 7
	0x01,		// 8
	0x21,		// 9
	0x7F,		// -
	0xFF,
	0xFF,
	0xFF,
	0xFF,
0xFF};	

volatile uint8_t digit[4];			// Displayed digits
volatile uint8_t buf[4];			// Receive buffer for next digits
volatile uint8_t swflag,switches;

//--------------------------------------------------------
// TWI interrupt handler
ISR(TWI_vect){
	uint8_t status,data,index,i;
	data=TWDR;						// Read data immediately.
	if(swflag==1)TWDR=switches;		// Copy switch reading to TWI data register.
	status=TWSR & 0xF8;
	TWCR = TWCR | (1<<TWINT);		// Clear interrupt to start operation of TWI hardware again.
	switch(status){
		case 0x60:					// Address and write command received.
			break;
		case 0x80:					// Data received.
			index=(data&0x30)>>4;
			buf[index]=seg[data&0x0F];
			if(data&0x40) buf[index] &= 0xFE;	// Set decimal point.
			if(data&0x80){
				for(i=0;i<4;i++) digit[i]=buf[i];	// Transfer new data to display.
			}
			break;
		case 0xA8:					// Address and read command received.
			swflag=0;
			PORTB=PORTB&0x7F;		// Clear Slave switch signal.
			break;
		case 0xB8:					// Data transmitted, ACK received.
		case 0xC0:					// Data transmitted, NAK received. 
									// Does not matter if we get ACK or NAK, but NAK is expected since it was the last byte.
		default:
			break;
			
	}			
}	

int main(void)
{
    uint8_t dig=0;
	uint8_t swon,swoff;

	//-------------------------------------------------------
	// Init TWI
	TWAR = SEGADDR<<1;				// 7-segment slave address is 0x33
	TWCR = (1<<TWINT)|(1<<TWEA)|(1<<TWEN)|(1<<TWIE);	// Prepare I2C slave to receive data
	
	//-------------------------------------------------------

	// Init I/O ports
	DDRD=0xFF;						// All port D pins are segment drivers
	PORTD=0xFF;						// All segments off
	DDRB=0x8F;						// Lower port B pins are digit drivers. Pin 7 is Slave switch signal (sss), active high
	PORTB=0x0F;						// All digits off, sss low.
	DDRC=0x00;						// Pin 0-3 are switch inputs. Pin 4 and 5 of port C are I2C pins.
	PORTC=0x3F;						// Pull-up for switches and I2C (NB! Between 20 and 50 k. External 2k2 is needed to comply with I2C spec.)
	
	
	
	sei();							// Turn on interrupts
	
	while(1)
    {
		 // Drive 7-segment display
		 PORTB=0x80&PORTB | (0x0F&~(1<<dig));	// Select digit, but do not disturb slave switch signal.
		 PORTD=digit[dig];				// Output segment data
		 dig++;							// Increment digit pointer
		 dig=dig&0x03;					// Roll over
		 
		 // Read and debounce switches
		 switches=(~PINC) & 0x0F;
		 if(switches){
			 swon++;
			 swoff=0;
			 if(swon==4){
				 PORTB=PORTB|0x80;
				 swflag=1;
			 }				 
			 if(swon>5)swon=5;
		 }
		 else{
			 swoff++;
			 if(swon<4)swon=0;
			 if(swoff>20){
				 swon=0;
				 swoff=20;
			 }
		 }
		 
		 _delay_ms(4);
    }
}