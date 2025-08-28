#include <stdio.h>
#include <stdlib.h>
#ifndef PIN_SIMULATION
#include <BBBiolib.h>
#endif
#include "pindef.h"
#include "pwm.h"

#undef PIN_DEFINE
#ifndef PIN_SIMULATION
#define PIN_DEFINE(namehm,definehm,definebbbio) \
	case definehm: \
		return definebbbio; \
		break;
#else
#define PIN_DEFINE(namehm,definehm,definebbbio) \
	case definehm: \
		return definehm; \
		break;
#endif
		

int getBBBPin(int hmPin)
{
	switch (hmPin)
	{
#include "pindef.def"		
		default:
			printf("Unknown Pin %d\n", hmPin);
			return -1;
		
	}
		
}	
