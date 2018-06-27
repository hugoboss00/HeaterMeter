#ifndef _SERIAL_H_
#define _SERIAL_H_

#define DEC 0
#define HEX 1

class Serial {
public:
	Serial();
	int available();
	void begin(int baud);
	void write(const char c);
	void write(const char *str);
	void write(int val, int base);
	void write(float val);
	char read();
	
	
};



#endif