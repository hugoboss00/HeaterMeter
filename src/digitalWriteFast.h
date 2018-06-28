

#define BIT_READ(value, bit) (((value) >> (bit)) & 0x01)
#define BIT_SET(value, bit) ((value) |= (1UL << (bit)))
#define BIT_CLEAR(value, bit) ((value) &= ~(1UL << (bit)))
#define BIT_WRITE(value, bit, bitvalue) (bitvalue ? BIT_SET(value, bit) : BIT_CLEAR(value, bit))

#ifndef digitalWriteFast
#define digitalWriteFast(P, V) \
do {                       \
digitalWrite((P), (V));         \
}while (0)
#endif  //#ifndef digitalWriteFast2

#if !defined(pinModeFast)
#define pinModeFast(P, V) \
do {\
pinMode((P), (V)); \
} while (0)
#endif


#ifndef noAnalogWrite
#define noAnalogWrite(P) \
do { \
	turnOffPWM((P));   \
} while (0)
#endif		


#ifndef digitalReadFast
	#define digitalReadFast(P) ( (int) _digitalReadFast_((P)) )
	#define _digitalReadFast_(P ) \
	digitalRead((P))
#endif

#ifndef analogWrite
#define analogWrite(P,V)
#endif