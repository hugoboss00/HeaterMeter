#ifndef _PWM_H_
#define _PWM_H_

#define FREQ_SERVO 50.0f

class Pwm {
public:
	Pwm();
	void init(int pin, float freq);
	void setValue(float duty);
	void setValue(int output, int servo_min, int servo_max);
	int  getValue();
	
private:
	int m_pin;
	float m_freq;
};



#endif