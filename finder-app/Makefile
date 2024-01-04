# Assignement 2 Makefile
# Create by: Ryan Hamor

CROSS_COMPILE :=

all: writer.c
	$(CROSS_COMPILE)gcc -g -Wall -c -o writer.o writer.c
	$(CROSS_COMPILE)gcc -g -Wall -I/ writer.o -o writer 

clean:
	rm -f *.o writer *.elf *.map