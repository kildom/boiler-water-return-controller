/*
 * OchPowr.c
 *
 * Created: 2019-10-02 11:14:23
 * Author : Dominik
 */ 

#include <stdbool.h>
#include <string.h>

#include <avr/io.h>

#include <util/delay.h>

#include <avr/interrupt.h>
#include <avr/eeprom.h>


#define BITS7(a,b,c,d,e,f,g) ((1 << (a)) | (1 << (b)) | (1 << (c)) | (1 << (d)) | (1 << (e)) | (1 << (f)) | (1 << (g)))
#define BITS6(a,b,c,d,e,f) BITS7(a,b,c,d,e,f,f)
#define BITS5(a,b,c,d,e) BITS7(a,b,c,d,e,e,e)
#define BITS4(a,b,c,d) BITS7(a,b,c,d,d,d,d)
#define BITS2(a,b) BITS7(a,b,b,b,b,b,b)

#define LED_BANK_0 0
#define LED_BANK_1 1
#define LED_BANK_ICONS 2
#define LED_BANK_OFF 3

#define LED_CHAR_EMPTY 10

#define LED_BOOK (1 << 1)
#define LED_CELC (1 << 2)
#define LED_UP   (1 << 3)
#define LED_DOWN (1 << 5)
#define LED_ZAW  (1 << 6)
#define LED_POMP (1 << 7)

static const uint8_t convTable[2][11] = {
	{
		BITS6(0,1,2,7,5,4),   // 0
		BITS2(2,7),           // 1
		BITS5(1,2,6,4,5),     // 2
		BITS5(1,2,6,7,5),     // 3
		BITS4(0,6,2,7),       // 4
		BITS5(1,0,6,7,5),     // 5
		BITS6(1,0,4,5,7,6),   // 6
		BITS4(0,1,2,7),       // 7
		BITS7(0,1,2,4,5,6,7), // 8
		BITS6(5,7,2,1,0,6),   // 9
		0,                    // empty
	},
	{
		BITS6(1,0,5,6,7,3),   // 0
		BITS2(0,5),           // 1
		BITS5(1,0,2,7,6),     // 2
		BITS5(1,0,2,5,6),     // 3
		BITS4(3,2,0,5),       // 4
		BITS5(1,3,2,5,6),     // 5
		BITS6(1,3,7,6,5,2),   // 6
		BITS4(3,1,0,5),       // 7
		BITS7(0,1,2,3,5,6,7), // 8
		BITS6(6,5,0,1,3,2),   // 9
		0,                    // empty
	}
};


static uint8_t bank;
static volatile uint8_t banks[3];

static volatile uint16_t btnState = 0;
static volatile uint16_t timer = 0;

ISR(TIMER1_COMPA_vect)
{
	uint8_t i;
	uint8_t bits;
	uint16_t btnStateCopy;
	uint16_t btnStateOld;
		
	bank++;
	if (bank > LED_BANK_OFF + 1)
	{
		timer++;
		bank = 0;
	}
	else if (bank >= LED_BANK_OFF)
	{
		// TODO: instead main loop shold check below condition periodically
		/*if (timerQueue && (int16_t)(timerQueue->fireTime - timer) <= 0)
		{
			QUEUE_PUSH_RAW(0x40);
		}*/
		return; // TODO: do not return, because also check buttons state
	}
	
	bits = banks[bank];
	for (i = 0; i < 8; i++)
	{
		if (bits & 1)
			PORTD &= ~(1 << 5);
		else
			PORTD |= 1 << 5;
		bits >>= 1;
		PORTD |= 1 << 6;
		PORTD &= ~(1 << 6);
	}
	
	switch (bank)
	{
		case LED_BANK_0:
			DDRB |= 1 << 0;
			break;
		case LED_BANK_1:
			DDRB |= 1 << 1;
			break;
		case LED_BANK_ICONS:
			DDRB |= 1 << 7;
			break;
	}
	
	if (!(PIND & (1 << 7))) btn[0]++; else btn[0]--;
	if (!(PINB & (1 << 5))) btn[1]++; else btn[1]--;
	if (!(PINB & (1 << 2))) btn[2]++; else btn[2]--;
	
	for (i = 0; i < 3; i++)
	{
		switch (btn[i])
		{
		case OFF_MAX: // 5
			btn_events[i]++; // PUSH
			// no break
		case ON_MAX: // 11
			btn[i] = ON_MAX - 1;
			break;
		case ON_MIN: // 6
			btn_events[i]++; // RELEASE
			// no break
		case OFF_MIN: // 0 - starting point - reset value
			btn[i] = OFF_MIN + 1;
			break;
		}
	}
}

static volatile uint8_t btn_events[3] = { 0, 0, 0 };

uint8_t popBtnEvent(uint8_t i)
{
	uint8_t num_events;
	uint8_t* ptr = &btn_events[i];
	cli();
	num_events = *ptr;
	if (num_events > 0) ptr = num_events - 1;
	sei();
	return num_events > 0;
}

static uint8_t btn_last_state[3] = { 0, 0, 0 };

typedef struct BtnTimer_tag
{
	Timer timer;
	uint8_t button_index;
} BtnTimer;

static BtnTimer btn_timers[3] = { {TIMER_INIT_VALUE, 0}, {TIMER_INIT_VALUE, 1}, {TIMER_INIT_VALUE, 2} };

void btnRepeated(Timer* t)
{
	uint8_t i = ((BtnTimer*)t)->button_index;
	onKeyEvent(KEY_REPEATED, i);
}

void pollBtnEvents(uint8_t i)
{
	uint8_t event = popBtnEvent(i);
	if (!event) return;
	if (btn_last_state[i])
	{
		btn_last_state[i] = 0;
		stopTimer(&btn_timers[i].timer);
		onKeyEvent(KEY_RELEASED, i);
	}
	else
	{
		btn_last_state[i] = 1;
		startInterval(&btn_timers[i].timer, btnRepeated, KEY_REPEAT_DELAY_TIME, KEY_REPEAT_TIME);
		onKeyEvent(KEY_PRESSED, i);
	}
}

uint32_t getTimer()
{
	static uint16_t lo = 0;
	static uint16_t hi = 0;
	uint16_t r;
	cli();
	r = timer;
	sei();
	if (r < lo) hi++;
	lo = r;
	return ((uint32_t)hi << 16) | (uint32_t)lo;
}


ISR(TIMER1_COMPB_vect)
{
	DDRB &= ~(1 << 0) & ~(1 << 1) & ~(1 << 7);
}

void ledInit()
{
	DDRD |= 1 << 5;
	DDRD |= 1 << 6;
	TCCR1A = (0 << WGM10);
	OCR1A = 0;
	OCR1B = 192;
	ICR1 = 249;
	TIFR = (1 << OCF1A) | (1 << OCF1B);
	TIMSK = (1 << OCIE1A) | (1 << OCIE1B);
	TCCR1B = (3 << WGM12) | (3 << CS10);
}

void btnInit()
{
	PORTD |= 1 << 7;
	PORTB |= 1 << 5;
	PORTB |= 1 << 2;
}

void relInit()
{
	DDRC |= 1 << 4;
	DDRC |= 1 << 2;
	DDRD |= 1 << 0;
}

static bool relZaworInvert = false;

void relZawor(int8_t dir)
{
	if (relZaworInvert) dir = -dir;
	if (dir < 0)
	{
		PORTD |= (1 << 0);
		PORTC &= ~(1 << 2);
	}
	else if (dir > 0)
	{
		PORTC |= (1 << 2);
		PORTD &= ~(1 << 0);
	}
	else
	{
		PORTC &= ~(1 << 2);
		PORTD &= ~(1 << 0);		
	}
}

void relPompa(bool on)
{
	if (on)
	{
		PORTC |= (1 << 4);
	}
	else
	{
		PORTC &= ~(1 << 4);
	}
}

static uint16_t lastMeasure;

void sensInit()
{
	ADMUX = (0 << REFS0) | (0 << MUX0);
	ADCSRA = (1 << ADEN) | (7 << ADPS0);
	lastMeasure = getTimer();
}

static int16_t window[16] = { 0, 0, 0, 0, };
static int16_t windowSum = 0;
int16_t sensTemp = 0;

void sensUpdate()
{
	uint16_t t = getTimer();
	if (t - lastMeasure >= 25)
	{
		ADCSRA |= (1 << ADSC);
		lastMeasure = t;
	}
	if (ADCSRA & (1 << ADIF)) {
		ADCSRA |= (1 << ADIF);
		int32_t x = (uint32_t)ADC;
		x = 770L * x - (403230L - 128L);
		int16_t y = x >> 8;
		windowSum -= window[15];
		windowSum += y;
		memmove(&window[1], &window[0], sizeof(window) - sizeof(window[0]));
		window[0] = y;
		sensTemp = (windowSum + 8) / 16;
	}
}

#define BIT_SET(var, mask, val) do { if (val) (var) |= (mask); else (var) &= ~(mask); } while(0)

int main(void)
{
    /* Replace with your application code */
	
	btnInit();
	ledInit();
	relInit();
	
	uint16_t c = 0;
	bool st = false;
	
	sei();
	
	banks[0] = 0xFF;
	banks[1] = 0xFF;
	banks[2] = 0xFF;
	
	_delay_ms(500);
	
	banks[0] = convTable[0][4];
	banks[1] = convTable[1][6];
	banks[2] = 0x00;
	
	OSCCAL = 0x9F;
	
	sensInit();
	
	ADCSRA |= (1 << ADSC);
	
    while (1)
    {
		sensUpdate();
		int16_t t = sensTemp;
		banks[0] = convTable[0][t % 10]; t /= 10;
		banks[1] = convTable[1][t % 10];
		
		/*if ((btnState & 0x00F) == 0x00F) relZawor(-1); else relZawor(0);
		if ((btnState & 0x0F0) == 0x0F0) relZawor(1); else relZawor(0);
		if ((btnState & 0xF00) == 0xF00) relPompa(true); else relPompa(false);*/
		/*cli();
		c = timer;
		if (c == 1000)
		{
			timer = 0;
			c = 0;
		}
		sei();
		if (ADCSRA & (1 << ADIF)) {
			ADCSRA |= (1 << ADIF);
			uint16_t x = ADC;
			banks[1] = convTable[1][LED_CHAR_EMPTY];
			banks[0] = convTable[0][x % 10]; x /= 10; _delay_ms(2000);
			banks[0] = convTable[0][x % 10]; x /= 10; _delay_ms(2000);
			banks[0] = convTable[0][x % 10]; x /= 10; _delay_ms(2000);
			banks[0] = convTable[0][x % 10]; x /= 10; _delay_ms(2000);
			banks[0] = convTable[0][x % 10]; x /= 10; _delay_ms(2000);
		}*/
		//if (c < 500) relPompa(1); else relPompa(0);
		/*updateScreen();
		_delay_us(500);
		ledBank(LED_BANK_OFF);
		_delay_us(1500);
		c++;
		if (c > 22)
		{
			c = 0;
			BIT_SET(banks[LED_BANK_ICONS], LED_BOOK, st);
			st = !st;
		}*/
    }
}

extern void mainLoop();

// Device inputs
void measureTemp();
uint16_t getTemp();
uint8_t getInput();

// Buttons input
uint8_t getButtonPulse(uint8_t buttonIndex);

// Device outputs
void setRelay(uint8_t relayIndex, uint8_t state);

// Display output
void setDisplay(uint8_t charIndex, uint8_t content);
void setBrightness(uint8_t value);

// Time input
uint32_t getTime();

// NV mem
void initConfig(void* ptr, uint16_t size);
void saveConfig(uint16_t offset, uint8_t size);

