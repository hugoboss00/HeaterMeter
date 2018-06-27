#ifndef _SYSTEMIF_H_
#define _SYSTEMIF_H_

#include <stddef.h>

#define 	_BV(bit)   (1 << (bit))
#define 	bit_is_set(sfr, bit)   ((sfr) & _BV(bit))
#define OUTPUT 0

#define PIN_SIMULATION

#ifdef PIN_SIMULATION
int pinget(char *key);
void pinset(char *key, int value);
#endif

unsigned int millis (void);
void delayMicroseconds(int us);
void digitalWrite(int pin, int on);
void pinMode(int pin, int mode);


#endif						   