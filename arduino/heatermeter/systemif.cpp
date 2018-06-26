#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include "systemif.h"
#include <unistd.h>


unsigned int millis (void)
{
    long            ms; // Milliseconds
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);

    ms  = spec.tv_sec *  1000;
    ms += round(spec.tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds

	return ms;
}						   

unsigned char pgm_read_byte(const unsigned char *buf)
{
	return *buf;
}

void delayMicroseconds(int us)
{
	int delay = us / 1000;
	if (delay == 0)
		delay = 1;
	usleep(delay);
	
}

void digitalWrite(int pin, int on)
{
}

void pinMode(int pin, int mode)
{
}


