#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "systemif.h"
#include <BBBiolib.h>
#include "pwm.h"



Pwm::Pwm()
{
}

void Pwm::init(int pin, float freq)
{
	m_freq = freq;
	m_pin = pin;
}


void Pwm::setValue(float duty)
{
	float period = 1/m_freq;
	float pw = period * duty / 100.0f;
	printf("pulsewidth : %f , duty : %f\n" ,pw ,duty);
    BBBIO_PWMSS_Setting(m_pin , m_freq ,duty , duty);
	BBBIO_ehrPWM_Enable(m_pin);
}


int  Pwm::getValue()
{
	return 0;
}


