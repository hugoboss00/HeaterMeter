#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "systemif.h"
#include <BBBiolib.h>
#include <pthread.h>
#include <sys/mman.h>
#include "adc.h"
#include "pinmux.h"

extern unsigned int *adctsc_ptr;

Adc::Adc()
{
	memset(m_adcValue, 0, sizeof(m_adcValue));
	memset(adcState, 0, sizeof(adcState));
}

#if 0
void ISR_task(int ADC_vect)
{
  if (adcState.discard != 0)
  {
    --adcState.discard;
    // Actually do the calculations for the previous set of reads while in the
    // discard period of the next set of reads. Break the code up into chunks
    // of roughly the same number of clock cycles.
    if (adcState.discard == 2)
    {
      adcState.analogReads[adcState.pin] = adcState.accumulator;
      adcState.analogRange[adcState.pin] = adcState.thisHigh - adcState.thisLow;
    }
    else if (adcState.discard == 1)
    {
      adcState.accumulator = 0;
      adcState.thisHigh = 0;
      adcState.thisLow = 0xff;
      adcState.pin = ADMUX;
    }
    else if (adcState.discard == 0)
    {
      if (adcState.pin == ADC_INTERLEAVE_HIGHFREQ)
      {
        adcState.cnt = 4;
        adcState.pin_next = (adcState.pin_next + 1) % NUM_ANALOG_INPUTS;
        // Notice this doesn't check if pin_next is ADC_INTERLEAVE_HIGHFREQ, which
        // means ADC_INTERLEAVE_HIGHFREQ will be checked twice in a row each loop
        // Not worth the extra code to make that not happen
      }
      else
        adcState.cnt = adcState.top;

    }
    return;
  }

  if (adcState.cnt != 0)
  {
    --adcState.cnt;
    unsigned int adc = ADC;
#if defined(NOISEDUMP_PIN)
    if ((ADMUX) == g_NoisePin)
      adcState.data[adcState.cnt] = adc;
#endif
    adcState.accumulator += adc;

    unsigned char a = adc >> 2;
    if (a > adcState.thisHigh)
      adcState.thisHigh = a;
    if (a < adcState.thisLow)
      adcState.thisLow = a;
  }
  else
  {
    unsigned char pin = ADMUX;

    // If just read the interleaved pin, advance to the next pin
    if (pin == ADC_INTERLEAVE_HIGHFREQ)
      pin = adcState.pin_next;
    else
      pin = ADC_INTERLEAVE_HIGHFREQ;

    ADMUX = pin;
    adcState.discard = 3;
  }
}
#endif
void Adc::filterAdc(int pin, int adc)
{

  if (adcState[pin].cnt != 0)
  {
    --adcState[pin].cnt;
  
    if (adcState[pin].isNoisePin)
      adcState[pin].data[adcState[pin].cnt] = adc;

    adcState[pin].accumulator += adc;

    unsigned char a = adc >> 2;
    if (a > adcState[pin].thisHigh)
      adcState[pin].thisHigh = a;
    if (a < adcState[pin].thisLow)
      adcState[pin].thisLow = a;
  }
  else
  {
      adcState[pin].analogRead = adcState[pin].accumulator;
      adcState[pin].analogRange = adcState[pin].thisHigh - adcState[pin].thisLow;
	  adcState[pin].cnt = adcState[pin].top;
      adcState[pin].accumulator = 0;
      adcState[pin].thisHigh = 0;
      adcState[pin].thisLow = 0xffffffff;
  }
}

#ifdef PIN_SIMULATION
void * Adc::adc_loop(void *argv)
{
	Adc *padc = (Adc *)argv;
	while(1)
	{
		for (int i=0; i < NUM_BBBIO_ADCS; i++)
		{
			if (padc->adcState[i].enabled)
			{
				char buf[20];
				sprintf(buf, "ADC%d", i);
				padc->filterAdc(i, pinget(buf));
			//padc->m_adcValue[i] = pinget(buf);
			}
		}
		delayMicroseconds(1000);
	}
}

#else
void * Adc::adc_loop(void *argv)
{
	Adc *padc = (Adc *)argv;
	while(1)
	{
		for (int i=0; i < NUM_BBBIO_ADCS; i++)
		{
			if (adcState[i].enabled)
			{
				BBBIO_ADCTSC_channel_enable(padc->m_pins[i]);
			}
		}
		BBBIO_ADCTSC_work(BUFFER_SIZE);
		for (int i=0; i < NUM_BBBIO_ADCS; i++)
		{
			if (adcState[i].enabled)
			{
				padc->filterAdc(i, padc->m_buffer[i][0]);
			}
		}
		delayMicroseconds(1000);
	}
}
#endif


void Adc::setTop(int newTop)
{
	printf("all setTop to %d\n", newTop);
	/* do not set new top value for not filtered pins */
	for (int i = 0; i < NUM_BBBIO_ADCS; i++)
	{
		if (adcState[i].top != 1)
			adcState[i].top = newTop;
	}
}


void Adc::setTop(int hmpin, int newTop)
{
	int pin = getBBBPin(hmpin);
	if (pin < 0)
	  return;
	printf("pin %d setTop to %d\n", pin, newTop);
	adcState[pin].top = newTop;
}


unsigned int Adc::analogReadOver(int hmpin, unsigned char bits)
{
  unsigned long a;
  int pin = getBBBPin(hmpin);
  if (pin < 0)
	  return 0;
  a = adcState[pin].analogRead;

  // If requesting a highfreq pin, scale down from reduced resolution
  if (adcState[pin].top == 1)
    return a >> (12 - bits);

  // Scale up to 256 samples then divide by 2^4 for 14 bit oversample
  unsigned int retVal = a * 16 / adcState[pin].top;
  return retVal >> (14 - bits);
}

unsigned int Adc::analogReadRange(int hmpin)
{
  int pin = getBBBPin(hmpin);
  if (pin < 0)
	  return 0;

  return adcState[pin].analogRange;
}



void Adc::init(int pin[], int adccount)
{
	const int clk_div = 160;
	const int open_dly = 0;
	const int sample_dly = 1;
	pthread_t adc_thread;

	int i;

	/*ADC work mode : Timer interrupt mode
	 *	Note : This mode handle SIGALRM using signale() function in BBBIO_ADCTSC_work();
	 */
	printf("ADC init %d\n", adccount);
	if ((adctsc_ptr != NULL) &&( adctsc_ptr != MAP_FAILED))
	{
		BBBIO_ADCTSC_module_ctrl(BBBIO_ADC_WORK_MODE_TIMER_INT, clk_div);
	}
	else
	{
		printf("BBBIO ADC Init FAILED\n");
	}

	for (i = 0; i < adccount; i++)
	{
		int bbbPin = getBBBPin(pin[i]);
		adcState[bbbPin].enabled = 1;
		printf("ADC init channel ADC%d\n", i);
		if ((adctsc_ptr != NULL) &&( adctsc_ptr != MAP_FAILED))
		{
			BBBIO_ADCTSC_channel_ctrl(bbbPin, BBBIO_ADC_STEP_MODE_SW_CONTINUOUS, open_dly, sample_dly, \
				BBBIO_ADC_STEP_AVG_1, m_buffer[i], BUFFER_SIZE);
		}
	}

	pthread_create(&adc_thread, NULL, &adc_loop, this);

	
}

void Adc::adcDump(void)
{
#if defined(NOISEDUMP_PIN)
  static unsigned char x;
  ++x;
  if (x == 5)
  {
    x = 0;
    ADCSRA = bit(ADEN) | bit(ADATE) | bit(ADPS2) | bit(ADPS1) | bit (ADPS0);
    CmdSerial.write("HMLG,NOISE ");
    for (unsigned int i=0; i<adcState.top; ++i)
    {
      CmdSerial.write(adcState.data[i], DEC);
    }
    Serial_nl();
    ADCSRA = bit(ADEN) | bit(ADATE) | bit(ADIE) | bit(ADPS2) | bit(ADPS1) | bit (ADPS0) | bit(ADSC);
  }
#endif
}

