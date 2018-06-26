#include "serial.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "systemif.h"


Serial::Serial()
{
}

int Serial::available()
{
	return 0;
}

void Serial::begin(int baud)
{
}

void Serial::write(const char c)
{
	printf("%c",c);
}


void Serial::write(const char *str)
{
	printf("%s",str);
}

void Serial::write(int val, int base)
{
	if (base == DEC)
	{
		printf("%d", val);
	}
	else
	{
		printf("0x%x", val);
	}
}

void Serial::write(float val)
{
	printf("%f",val);
}
	
char Serial::read()
{
	return 0;
}

