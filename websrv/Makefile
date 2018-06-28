TARGET = libwebserver.a
LIBS = -lpthread -lboost_system -lboost_filesystem 
#-lboost_chrono -lboost_date_time -lboost_atomic  -lboost_thread
CC = g++
CFLAGS = -std=c++11 -g -Wall
LFLAGS = -Wall

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.cpp, %.o, $(wildcard *.cpp))
HEADERS = $(wildcard */*.hpp)

%.o: %.cpp $(HEADERS)
	$(CC) $(CFLAGS) -fpermissive -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)

example: $(OBJECTS)
	$(CC) $(OBJECTS) $(LFLAGS) $(LIBS) -o $@

	
clean:
	-rm -f *.o $(OBJECTS)
	-rm -f $(TARGET)

	