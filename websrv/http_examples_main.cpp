#include <stdlib.h>
#include <stdio.h>

extern void start_server();
extern void wait_server();

int main()
{
	start_server();
	wait_server();
}
