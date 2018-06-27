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


static tSIMPIN *simpins = NULL;

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
	memset(ptr, 0, SIZE);
	return 0;
}


int main(int argc, char **argv)
{
	if (init_shm() != 0)
		return 0;
	
	while (1)
	{
		int index = 0;
		printf("\033[2J");
		while ((index < 32) && (simpins[index].name[0] != 0))
		{
			printf("%s: %d\n",simpins[index].name, simpins[index].value);
			index++;
		}
		sleep(1);
	}
		
	return 0;
}
