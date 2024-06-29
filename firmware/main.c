/*
 * USBasp - USB in-circuit programmer for Atmel AVR controllers
 *
 * Thomas Fischl <tfischl@gmx.de>
 *
 * License........: GNU GPL v2 (see Readme.txt)
 * Target.........: ATMega8 at 12 MHz
 * Creation Date..: 2005-02-20
 * Last change....: 2009-02-28
 *
 * PC2 SCK speed option.
 * GND  -> slow (8khz SCK),
 * open -> software set speed (default is 375kHz SCK)
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>

#include "usbasp.h"
#include "usbdrv.h"

static volatile uchar ready = 0;
static uchar phase = 0;
static uchar reportBuffer[3] = {0, 1, 1};

const char sintable[64] = {
	0x00,
	0x03,
	0x06,
	0x09,
	0x0C,
	0x0F,
	0x12,
	0x15,
	0x18,
	0x1C,
	0x1F,
	0x22,
	0x25,
	0x28,
	0x2B,
	0x2E,
	0x30,
	0x33,
	0x36,
	0x39,
	0x3C,
	0x3F,
	0x41,
	0x44,
	0x47,
	0x49,
	0x4C,
	0x4E,
	0x51,
	0x53,
	0x55,
	0x58,
	0x5A,
	0x5C,
	0x5E,
	0x60,
	0x62,
	0x64,
	0x66,
	0x68,
	0x6A,
	0x6C,
	0x6D,
	0x6F,
	0x70,
	0x72,
	0x73,
	0x74,
	0x76,
	0x77,
	0x78,
	0x79,
	0x7A,
	0x7B,
	0x7C,
	0x7C,
	0x7D,
	0x7E,
	0x7E,
	0x7F,
	0x7F,
	0x7F,
	0x7F,
	0x7F
};

// taken from HID Descriptor Tool
PROGMEM const char usbHidReportDescriptor[] = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,                    // USAGE (Mouse)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x09, 0x01,                    //   USAGE (Pointer)
    0xa1, 0x00,                    //   COLLECTION (Physical)
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
    0x29, 0x03,                    //     USAGE_MAXIMUM (Button 3)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x95, 0x03,                    //     REPORT_COUNT (3)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x75, 0x05,                    //     REPORT_SIZE (5)
    0x81, 0x03,                    //     INPUT (Cnst,Var,Abs)
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                    //     USAGE (X)
    0x09, 0x31,                    //     USAGE (Y)
    0x15, 0x81,                    //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,                    //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x02,                    //     REPORT_COUNT (2)
    0x81, 0x06,                    //     INPUT (Data,Var,Rel)
    0xc0,                          //   END_COLLECTION
    0xc0                           // END_COLLECTION
};

ISR(TIMER0_OVF_vect)
{
	ready = 1;
	++phase;
	uchar phasey = phase + 0x40;

	char signx = phase & 0x80 ? -1 : 1;
	char mirx = phase & 0x40;
	char signy = phasey & 0x80 ? -1 : 1;
	char miry = phasey & 0x40;

	uchar ix = phase & 0x3f;
	if (mirx) ix = 0x3f - ix;
	uchar iy = phasey & 0x3f;
	if (miry) iy = 0x3f - iy;

	char vx = sintable[ix];
	char vy = sintable[iy];
	reportBuffer[1] = signx * (vx >> 4);
	reportBuffer[2] = signy * (vy >> 4);
}

uchar usbFunctionSetup(uchar data[8]) {
	usbRequest_t *rq = (void *)data;

	usbMsgPtr = reportBuffer;
	if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) { 
		if(rq->bRequest == USBRQ_HID_GET_REPORT) {  
			return sizeof(reportBuffer);
		}
	}
	return 0;
}

int main(void) {
	uchar i, j;

	/* no pullups on USB and ISP pins */
	PORTD = 0;
	PORTB = 0;
	/* all outputs except PD2 = INT0 */
	DDRD = ~(1 << 2);

	/* output SE0 for USB reset */
	DDRB = ~0;
	j = 0;
	/* USB Reset by device only required on Watchdog Reset */
	while (--j) {
		i = 0;
		/* delay >10ms for USB reset */
		while (--i)
			;
	}
	/* all USB and ISP pins inputs */
	DDRB = 0;

	/* all inputs except PC0, PC1 */
	DDRC = 0x03;
	PORTC = 0xfe;

	// configure timer0 (prescaler 256 or 1024, interrupt on)
	if ((PINC & (1 << PC2)) == 0)	// SCK jumper
		TCCR0 |= (1 << CS02) | (1 << CS00);
	else
		TCCR0 |= (1 << CS02);
	TIMSK |= (1 << TOIE0);

	/* main event loop */
	usbInit();
	sei();
	for (;;) {
		usbPoll();
		if (ready && usbInterruptIsReady()) {
			ready = 0;
			usbSetInterrupt(reportBuffer, sizeof(reportBuffer));
		}
	}
	return 0;
}

