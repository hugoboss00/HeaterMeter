
LIBS = -lm -lstdc++ -lrt -pthread -lBBBio
CC = gcc
CFLAGS = -I ../../bbbio/BBBIOlib/BBBio_lib -g -Wall
LFLAGS = -Wall -L ../../bbbio/BBBIOlib

.PHONY: default all clean

default: hm
all: default


HMOBJECTS += $(patsubst %.cpp, %.o, $(wildcard src/*.cpp))
HMHEADERS = $(wildcard src/*.h)

%.o: %.c $(HMHEADERS)
	$(CC) $(CFLAGS) -Wno-pointer-to-int-cast -c $< -o $@

%.o: %.cpp $(HMHEADERS)
	$(CC) $(CFLAGS) -fpermissive -c $< -o $@

.PRECIOUS: hm $(HMOBJECTS)

hm: $(HMOBJECTS)
	$(CC) $(HMOBJECTS) $(LFLAGS) $(LIBS) -o $@

clean:
	-rm -f *.o $(HMOBJECTS)
	-rm -f hm

	