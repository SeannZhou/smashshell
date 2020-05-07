CC = gcc
CFLAGS = -Wall -Werror

all: exe

exe: smash.o
	$(CC) $(CFLAGS) smash.o -o smash
	rm smash.o

smash.o:
	$(CC) $(CFLAGS) -c smash.c

.PHONEY: clean debug

clean:
	rm testtext.txt