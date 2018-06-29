TARGET = libgomsrv.a
LIBS = -lpthread -lboost_system -lboost_filesystem 
#-lboost_chrono -lboost_date_time -lboost_atomic  -lboost_thread
CC = g++
CFLAGS = -std=c++11 -O2 -Wall
LFLAGS = -Wall

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = gom_server.o
HEADERS = $(wildcard */*.hpp)

%.o: %.cpp $(HEADERS)
	$(CC) $(CFLAGS) -fpermissive -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)

gom_server_test: $(OBJECTS) gom_server_test.o
	$(CC) $(OBJECTS) gom_server_test.o $(LFLAGS) $(LIBS) -o $@

	
clean:
	-rm -f *.o $(OBJECTS)
	-rm -f $(TARGET)

	