#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "systemif.h"
#ifndef PIN_SIMULATION
#include <BBBiolib.h>
#endif
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
	  // printf("adc new val(%d):%d\n", pin, adcState[pin].accumulator);
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
		// wait 1ms
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
			if (padc->adcState[i].enabled)
			{
				//printf("enable channel for pin %d\n",getBBBPin(i));
				BBBIO_ADCTSC_channel_enable(getBBBPin(i));
			}
		}
		BBBIO_ADCTSC_work(BUFFER_SIZE); 
		
		for (int i=0; i < NUM_BBBIO_ADCS; i++)
		{
			if (padc->adcState[i].enabled)
			{
				padc->filterAdc(i, padc->m_buffer[i][0]);
			}
		}
		// wait 1ms
		delayMicroseconds(5000);
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
  
  if (!adcState[pin].enabled)
	  return 0;
  a = adcState[pin].analogRead;

  // If requesting a highfreq pin, scale down from reduced resolution
  if (adcState[pin].top == 1)
    return a >> (12 - bits);

  // Scale up to 256 samples then divide by 2^4 for 14 bit oversample
  unsigned int retVal = a * 4 / adcState[pin].top;
  //printf("ADC%d: %d\n", pin, retVal >> (14 - bits));
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
#ifndef PIN_SIMULATION
	if ((adctsc_ptr != NULL) &&( adctsc_ptr != MAP_FAILED))
	{
		BBBIO_ADCTSC_module_ctrl(BBBIO_ADC_WORK_MODE_BUSY_POLLING, clk_div);
		//BBBIO_ADC_WORK_MODE_TIMER_INT
	}
	else
	{
		printf("BBBIO ADC Init FAILED\n");
	}

	for (i = 0; i < adccount; i++)
	{
		int bbbPin = getBBBPin(pin[i]);
		adcState[bbbPin].enabled = 1;
		printf("ADC init channel ADC%d\n", bbbPin);
		if ((adctsc_ptr != NULL) &&( adctsc_ptr != MAP_FAILED))
		{
			printf("init channel for pin %d\n", bbbPin);
			BBBIO_ADCTSC_channel_ctrl(bbbPin, BBBIO_ADC_STEP_MODE_SW_CONTINUOUS, open_dly, sample_dly, \
				BBBIO_ADC_STEP_AVG_1, m_buffer[bbbPin], BUFFER_SIZE);
		}
	}
#endif
	pthread_create(&adc_thread, NULL, &adc_loop, this);
	pthread_setname_np(adc_thread, "gom_adc");
}

void Adc::adcDump(void)
{
}

