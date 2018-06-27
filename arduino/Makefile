TARGET = hm
LIBS = -lm -lstdc++ -lrt -pthread -lBBBio
CC = gcc
CFLAGS = -I ../../../bbbio/BBBIOlib/BBBio_lib -g -Wall
LFLAGS = -Wall -L ../../../bbbio/BBBIOlib

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard */*.c))
OBJECTS += $(patsubst %.cpp, %.o, $(wildcard */*.cpp))
HEADERS = $(wildcard */*.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -Wno-pointer-to-int-cast -c $< -o $@

%.o: %.cpp $(HEADERS)
	$(CC) $(CFLAGS) -fpermissive -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LFLAGS) $(LIBS) -o $@

clean:
	-rm -f *.o $(OBJECTS)
	-rm -f $(TARGET)

	