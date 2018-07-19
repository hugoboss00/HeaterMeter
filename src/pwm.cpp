#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "systemif.h"
#include <pthread.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <BBBiolib.h>
using namespace std;
#include "pwm.h"



Pwm::Pwm()
{
	m_current_dc = 0;
	m_target_dc = -1;
	m_locktarget = 0;
}

void Pwm::lock(int time_sec)
{
	m_locktarget = time(NULL) + time_sec;
	
}

void Pwm::initController()
{
	int periodns = 1000000000 / m_freq;
	write(m_chipPath, "export", m_channel);
	write(m_controllerPath, "period", periodns);
	write(m_controllerPath, "enable", 1);
}


void Pwm::init(int pin, float freq)
{
	m_freq = freq;
	m_pin = pin;
	pthread_t pwm_thread;

	switch (pin)
	{
		case PWM_PIN0A:
			m_chipPath = "/sys/class/pwm/pwmchip0";
			m_controllerPath = "/sys/class/pwm/pwm-0:0";
			m_channel = 0;
			break;
		case PWM_PIN0B:
			m_chipPath = "/sys/class/pwm/pwmchip0";
			m_channel = 1;
			m_controllerPath = "/sys/class/pwm/pwm-0:1";
			break;
		case PWM_PIN1A:
			m_chipPath = "/sys/class/pwm/pwmchip2";
			m_channel = 0;
			m_controllerPath = "/sys/class/pwm/pwm-2:0";
			break;
		case PWM_PIN1B:
			m_chipPath = "/sys/class/pwm/pwmchip2";
			m_channel = 1;
			m_controllerPath = "/sys/class/pwm/pwm-2:1";
			break;
		case PWM_PIN2A:
			m_chipPath = "/sys/class/pwm/pwmchip4";
			m_channel = 0;
			m_controllerPath = "/sys/class/pwm/pwm-4:0";
			break;
		case PWM_PIN2B:
			m_chipPath = "/sys/class/pwm/pwmchip4";
			m_channel = 1;
			m_controllerPath = "/sys/class/pwm/pwm-4:1";
			break;
			
	}

	initController();
	pthread_create(&pwm_thread, NULL, &pwm_loop, this);
	pthread_setname_np(pwm_thread, "gom_pwm");

}

#if 0
void Pwm::setValue(float duty)
{
	float period = 1/m_freq;
	float pw = period * duty / 100.0f;
	//printf("pwm pulsewidth : %f , duty : %f\n" ,pw ,duty);
#ifdef PIN_SIMULATION
#else
    BBBIO_PWMSS_Setting(m_pin , m_freq ,duty , duty);
	BBBIO_ehrPWM_Enable(m_pin);
#endif
}
#endif 
void Pwm::setValue(int dutyns, int fast)
{
	if (m_locktarget < time(NULL))
	{
		//printf("pwm pulsewidth : %d ns\n" ,dutyns);
		if (fast)
		{
			m_target_dc = m_current_dc = dutyns;
			write(m_controllerPath, "duty_cycle", dutyns);
		}
		else
		{
			if (m_target_dc < 0)
			{
				write(m_controllerPath, "duty_cycle", dutyns);
				m_target_dc = m_current_dc = dutyns;
			}
			else
			{
				m_target_dc = dutyns;
			}
		}
	}
}

int  Pwm::getValue()
{
	return 0;
}

int Pwm::write(string path, string filename, string value){
   ofstream fs;
#ifdef PIN_SIMULATION
#else
   fs.open((path + "/" + filename).c_str());
   if (!fs.is_open()){
	   printf("%s:\n",(path + "/" + filename).c_str());
	   perror("PWM: write failed to open file ");
	   return -1;
   }
   fs << value;
   fs.close();
#endif
   return 0;
}

int Pwm::write(string path, string filename, int value){
   stringstream s;
   s << value;
   //printf("write %d to pwm\n", value);
   return write(path,filename,s.str());
}

void * Pwm::pwm_loop(void *argv)
{
	Pwm *ppwm = (Pwm *)argv;
	while(1)
	{
		// do not write if target and current are already identical
		if (ppwm->m_target_dc > 0)
		{
			if (ppwm->m_target_dc > ppwm->m_current_dc)
			{
				ppwm->m_current_dc+=1000; // + 1us
				ppwm->write(ppwm->m_controllerPath, "duty_cycle", ppwm->m_current_dc);
			}
			if (ppwm->m_target_dc < ppwm->m_current_dc)
			{
				ppwm->m_current_dc-= 1000; // -1us
				ppwm->write(ppwm->m_controllerPath, "duty_cycle", ppwm->m_current_dc);
			}
		}
		delayMicroseconds(1000);
	}
}
