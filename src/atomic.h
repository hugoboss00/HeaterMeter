#ifndef _ATOMIC_H_
#define _ATOMIC_H_

#define ATOMIC_FORCEON uint8_t sreg_save \
       __attribute__((__cleanup__(__iSeiParam))) = 0

#if 0
#define ATOMIC_BLOCK(type) for ( type, __ToDo = __iCliRetVal(); \
	                       __ToDo ; __ToDo = 0 )
#else
#define ATOMIC_BLOCK(type)
#endif
						   
#endif						   