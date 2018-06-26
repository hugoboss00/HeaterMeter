#ifndef _ADC_H_
#define _ADC_H_

#include <BBBiolib.h>

#define NUM_BBBIO_ADCS (BBBIO_ADC_AIN6 + 1)
#define BUFFER_SIZE 1

class Adc {
public:
	Adc();
	void init(int pin[], int adccount);
	int  getValue(int pin);

private:
	int m_pins[NUM_BBBIO_ADCS];
	int m_adccount;
	unsigned int m_buffer[NUM_BBBIO_ADCS][BUFFER_SIZE];
};



#endif