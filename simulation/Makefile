TARGET = sim_mon simset
LIBS = -lm -lstdc++ -lrt
CC = gcc
CFLAGS = -g -Wpointer-to-int-cast
LFLAGS = -Wall

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard */*.c))
OBJECTS += $(patsubst %.cpp, %.o, $(wildcard */*.cpp))
HEADERS = $(wildcard */*.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

sim_mon: sim_mon.o
	$(CC) sim_mon.o $(LFLAGS) $(LIBS) -o $@

simset: sim_set.o
	$(CC) sim_set.o $(LFLAGS) $(LIBS) -o $@

clean:
	-rm -f *.o
	-rm -f $(TARGET)

	