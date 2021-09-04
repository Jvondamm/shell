
all: mush
.PHONY: all

CC = gcc
CFLAGS = -Wall -g -pedantic


mush: mush.o
	$(CC) $(CFLAGS) mush.o -o  mush

mush.o: mush.c
	$(CC) $(CFLAGS) -c mush.c

clean: 
	rm *.o mush
