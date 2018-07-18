#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "systemif.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <BBBiolib.h>
using namespace std;
#include "pwm.h"



Pwm::Pwm()
{
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
void Pwm::setValue(int dutyns)
{
	printf("pwm pulsewidth : %d ns\n" ,dutyns);
	write(m_controllerPath, "duty_cycle", dutyns);
}

int  Pwm::getValue()
{
	return 0;
}

int Pwm::write(string path, string filename, string value){
   ofstream fs;
   fs.open((path + "/" + filename).c_str());
   if (!fs.is_open()){
	   printf("%s:\n",(path + "/" + filename).c_str());
	   perror("PWM: write failed to open file ");
	   return -1;
   }
   fs << value;
   fs.close();
   return 0;
}

int Pwm::write(string path, string filename, int value){
   stringstream s;
   s << value;
   return write(path,filename,s.str());
}
