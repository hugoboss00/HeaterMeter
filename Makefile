
LIBS = -Wl,--start-group -lm -lstdc++ -lrt -pthread -lboost_system -lboost_filesystem -lBBBio -lgomsrv -Wl,--end-group
CC = gcc
CFLAGS = -O2 -I ../../bbbio/BBBIOlib/BBBio_lib -I websrv -g -Wall
LFLAGS = -Wall -L ../../bbbio/BBBIOlib -L websrv

.PHONY: default all clean websrv

default: websrv hm

all: default


HMOBJECTS += $(patsubst %.cpp, %.o, $(wildcard src/*.cpp))
HMHEADERS = $(wildcard src/*.h)

%.o: %.c $(HMHEADERS)
	$(CC) $(CFLAGS) -Wno-pointer-to-int-cast -c $< -o $@

%.o: %.cpp $(HMHEADERS)
	$(CC) $(CFLAGS) -fpermissive -c $< -o $@

.PRECIOUS: hm $(HMOBJECTS)

websrv:
	$(MAKE) -C websrv

hm: $(HMOBJECTS) websrv
	$(CC) $(HMOBJECTS) $(LFLAGS) $(LIBS) -o $@

clean:
	-rm -f *.o $(HMOBJECTS)
	-rm -f hm

	