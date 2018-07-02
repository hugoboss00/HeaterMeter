#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>
#include "systemif.h"
#include <unistd.h>
#include <sys/mman.h>

#ifdef PIN_SIMULATION
typedef struct {
	char name[16];
	int  value;
} tSIMPIN;

static tSIMPIN *simpins = NULL;

int init_shm()
{
	if (simpins == NULL)
	{
		/* the size (in bytes) of shared memory object */
		const int SIZE = 4096;
	 
		/* name of the shared memory object */
		const char* name = "/SIMPIN";
	 
		/* shared memory file descriptor */
		int shm_fd;
	 
		/* pointer to shared memory obect */
		void* ptr;
	 
		/* create the shared memory object */
		shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
		if (shm_fd <= 0)
			perror("shm_open");
		/* configure the size of the shared memory object */
		if (ftruncate(shm_fd, SIZE) < 0)
		{
			simpins = NULL;
			return -1;
		}
	 
		/* memory map the shared memory object */
		ptr = mmap(0, SIZE, PROT_WRITE, MAP_SHARED, shm_fd, 0);
		if (ptr == MAP_FAILED)
		{
			printf("Error to map shared memory\n");
			perror("mmap");
			simpins = NULL;
			return -1;
		}
		simpins	= (tSIMPIN *)ptr;
		memset(ptr, 0, SIZE);
	}
	return 0;
}

void pinset(char *key, int value)
{
	int index = 0;
	init_shm();
	while ((index < 32) && (simpins[index].name[0] != 0))
	{
		if (strncmp(simpins[index].name, key, sizeof(simpins[index].name) ) == 0)
		{
			simpins[index].value = value;
			return;
		}
		index++;
	}
	if (index < 32)
	{
		simpins[index].value = value;
		strncpy(simpins[index].name, key, sizeof(simpins[index].name)-1);
		return;
	}
}

int pinget(char *key)
{
	int value = 0;
	int index = 0;
	init_shm();
	while ((index < 32) && (simpins[index].name[0] != 0))
	{
		if (strncmp(simpins[index].name, key, sizeof(simpins[index].name) ) == 0)
		{
			value = simpins[index].value;
		}
		index++;
	}
	return value;
}

#endif

unsigned int millis (void)
{
    long            ms; // Milliseconds
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);

    ms  = spec.tv_sec *  1000;
    ms += round(spec.tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds

	return ms;
}						   


void delayMicroseconds(int us)
{
	struct timespec a;

	a.tv_nsec=(us) * 1000L;
	a.tv_sec=0;
	if (nanosleep(&a, NULL) != 0) {
		perror("delay_ms error:");
	}

#if 0
	int delay = us ;
	if (delay == 0)
		delay = 1;
	usleep(delay);
#endif
}

void digitalWrite(int pin, int on)
{
#ifdef PIN_SIMULATION
	char buf[20];
	sprintf(buf, "gpio%d", pin);
	pinset(buf, on);
#else
#endif
}

void pinMode(int pin, int mode)
{
#ifdef PIN_SIMULATION
#else
#endif
}


