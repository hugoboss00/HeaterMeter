#include "serial.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "systemif.h"
#include <unistd.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>

Serial::Serial()
{
	fcntl (0, F_SETFL, O_NONBLOCK);
}

int Serial::available()
{
	return 0;
}

void Serial::begin(int baud)
{
}

void Serial::write(const char c)
{
	printf("%c",c);
}


void Serial::write(const char *str)
{
	printf("%s",str);
}

void Serial::write(int val, int base)
{
	if (base == DEC)
	{
		printf("%d", val);
	}
	else
	{
		printf("0x%x", val);
	}
}

void Serial::write(float val)
{
	printf("%f",val);
}
	
char Serial::read()
{
      int c=0;
#if 1
    struct termios orig_term_attr;
    struct termios new_term_attr;

    /* set the terminal to raw mode */
    tcgetattr(fileno(stdin), &orig_term_attr);
    memcpy(&new_term_attr, &orig_term_attr, sizeof(struct termios));
    new_term_attr.c_lflag &= ~(ECHO|ICANON);
    new_term_attr.c_cc[VTIME] = 0;
    new_term_attr.c_cc[VMIN] = 0;
    tcsetattr(fileno(stdin), TCSANOW, &new_term_attr);

    /* read a character from the stdin stream without blocking */
    /*   returns EOF (-1) if no character is available */
    c = fgetc(stdin);

    /* restore the original terminal attributes */
    tcsetattr(fileno(stdin), TCSANOW, &orig_term_attr);

	  if (c < 0)
		  c=0;
#endif
      return(c);
}

