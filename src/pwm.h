#ifndef _PWM_H_
#define _PWM_H_

#include <string>
using namespace std;

#define FREQ_SERVO 50.0f
#define PWM_PIN0A 1
#define PWM_PIN0B 2
#define PWM_PIN1A 3
#define PWM_PIN1B 4
#define PWM_PIN2A 5
#define PWM_PIN2B 6


class Pwm {
public:
	Pwm();
	void init(int pin, float freq);
	//void setValue(float duty);
	void setValue(int dutyns);
	int  getValue();
	
private:
	int write(string path, string filename, string value);
	int write(string path, string filename, int value);
	void initController();
	int m_pin;
	float m_freq;
	int m_channel;
	string m_chipPath;
	string m_controllerPath;
};



#endif