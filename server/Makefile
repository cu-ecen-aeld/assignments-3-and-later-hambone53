# Make file for aesdsocket program
# Created by: Ryan Hamor

all: aesdsocket

aesdsocket.o: aesdsocket.c
	$(CC) $(CCFLAGS) -c aesdsocket.c

aesdsocket: aesdsocket.o
	$(CC) $(LDFLAGS) aesdsocket.o -o aesdsocket -lrt -pthread

clean:
	rm -f *.o aesdsocket *.elf *.map