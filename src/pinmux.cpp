#include <stdio.h>
#include <stdlib.h>
#include <BBBiolib.h>
#include "pindef.h"

#undef PIN_DEFINE
#define PIN_DEFINE(namehm,definehm,definebbbio) \
	case definehm: \
		return definebbbio; \
		break;

		

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
