#ifndef _ADC_H_
#define _ADC_H_

#include <BBBiolib.h>

#define NUM_BBBIO_ADCS (BBBIO_ADC_AIN6 + 1)
#define BUFFER_SIZE 1


// ADC pin to poll between every other ADC read, with low oversampling
#define ADC_INTERLEAVE_HIGHFREQ 0

typedef struct 
{
  unsigned char top;       // Number of samples to take per reading
  unsigned char cnt;       // count left to accumulate
  unsigned int accumulator;  // total
  unsigned int thisHigh;  // High this period
  unsigned int thisLow;   // Low this period
  unsigned int analogRead; // Current values
  unsigned int analogRange; // high-low on last period
  unsigned int enabled;
  unsigned int isNoisePin;
  unsigned int data[256];
} tAdcState;




class Adc {
public:
	Adc();
	void init(int pin[], int adccount);
	unsigned int analogReadOver(int hmpin, unsigned char bits);
	unsigned int analogReadRange(int hmpin);
	void setTop(int newTop);
	void setTop(int hmpin, int newTop);
	void filterAdc(int index, int value);
	static void * adc_loop(void *argv);
	void adcDump(void);

private:
	unsigned int m_buffer[NUM_BBBIO_ADCS][BUFFER_SIZE + 1];
	unsigned int m_adcValue[NUM_BBBIO_ADCS];

public:
	tAdcState adcState[NUM_BBBIO_ADCS];
};

extern Adc adc;

#endif