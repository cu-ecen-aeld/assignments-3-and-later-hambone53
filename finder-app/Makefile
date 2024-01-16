# Assignement 2 Makefile
# Create by: Ryan Hamor

all: writer

writer.o: writer.c
	$(CC) $(CCFLAGS) -c writer.c

writer: writer.o
	$(CC) $(LDFLAGS) writer.o -o writer

clean:
	rm -f *.o writer *.elf *.map