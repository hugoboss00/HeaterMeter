#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "systemif.h"
#include <BBBiolib.h>
#include "adc.h"



Adc::Adc()
{
}


void Adc::init(int pin[], int adccount)
{
	const int clk_div = 160;
	const int open_dly = 0;
	const int sample_dly = 1;

	int i;

	m_adccount = adccount;
	/*ADC work mode : Timer interrupt mode
	 *	Note : This mode handle SIGALRM using signale() function in BBBIO_ADCTSC_work();
	 */
	printf("ADC init %d\n", adccount);
	BBBIO_ADCTSC_module_ctrl(BBBIO_ADC_WORK_MODE_TIMER_INT, clk_div);

	for (i = 0; i < m_adccount; i++)
	{
		printf("ADC init channel %d\n", i);
		BBBIO_ADCTSC_channel_ctrl(BBBIO_ADC_AIN0, BBBIO_ADC_STEP_MODE_SW_CONTINUOUS, open_dly, sample_dly, \
				BBBIO_ADC_STEP_AVG_4, m_buffer[i], BUFFER_SIZE);
	}
}

int Adc::getValue(int pin)
{
	BBBIO_ADCTSC_channel_enable(pin);
	BBBIO_ADCTSC_work(pin);
	return m_buffer[pin][0];
}
