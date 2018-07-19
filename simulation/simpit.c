#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include "simulation.h"
#include <sys/mman.h>


tSIMPIN *simpins = NULL;
float Steinhart[4]={5.36924e-4,1.91396e-4,6.60399e-8,1.0e+4};

int init_shm()
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
    ftruncate(shm_fd, SIZE);
 
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
	return 0;
}

void pinset(char *key, int value)
{
	int index = 0;
	while ((index < 32) && (simpins[index].name[0] != 0))
	{
		if (strncmp(simpins[index].name, key, sizeof(simpins[index].name) ) == 0)
		{
			printf("change %s from %d to %d\n",simpins[index].name, simpins[index].value, value);
			simpins[index].value = value;
			return;
		}
		index++;
	}
	if (index < 32)
	{
		printf("set %s from %d to %d\n",key, simpins[index].value, value);
		simpins[index].value = value;
		strncpy(simpins[index].name, key, sizeof(simpins[index].name)-1);
		return;
	}
}

int pinget(char *key)
{
	int value = 0;
	int index = 0;
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


float calcTemp(unsigned int adcval)
{
  const float ADCmax = 1023 * 4;

  /* if PROBETYPE_INTERNAL */
  float R, T;
  R = Steinhart[3] / ((ADCmax / (float)adcval) - 1.0f);

  // Compute degrees K
  R = log(R);
  T = 1.0f / ((Steinhart[2] * R * R + Steinhart[1]) * R + Steinhart[0]);

  return(T - 273.15f);
}

int calcAdc(float temperatur)
{
	int adc=1023 * 4;
	float calc=0.0;
	while ((adc > 0) && (calc < temperatur))
		calc = calcTemp(--adc);
	
	return adc;
}




double time_constant = 1.0; // degrees per minute
double temperature = 90.0;
int main(int argc, char **argv)
{
	int c;
	char *key;
	int value;
	int argok = 0;

	while ((c = getopt (argc, argv, "v:t:")) != -1)
	{
	switch (c)
	  {
	  case 'v':
		time_constant = atof(optarg);
		argok++;
		break;
	  case 't':
		temperature = atof(optarg);
		argok++;
		break;

	default:
		break;
	  }
	}


	if (init_shm() != 0)
		return 0;

	double deltat = 0.0;
	int intervall = 1;
	printf("Pit Simulation using %f degrees/min, starting temperatur: %f degrees\n", time_constant, temperature);
	while (1)
	{
		int servo = pinget("SERVO");
		int adc = calcAdc(temperature);
		printf("temp:%f, adc:%d, temp2:%f\n", temperature, adc, calcTemp(adc));
		deltat += ((-50.0 + servo)/50.0) * ((float)time_constant*intervall / 60.0);
		printf("add %f to temperatur, servo:%d\n", deltat, servo);
		pinset("ADC5", adc);
		temperature += deltat;
		sleep(intervall);
	}


	return 0;
}
