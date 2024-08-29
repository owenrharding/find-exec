CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=gnu99
CSSE2310A3 = -I/local/courses/csse2310/include -L/local/courses/csse2310/lib -lcsse2310a3
.PHONY = debug clean
DEBUG = -g
debug: CFLAGS += $(DEBUG)
debug: all
	
	
all: uqfindexec
	
uqfindexec: uqfindexec.c
	$(CC) $(CFLAGS) $(CSSE2310A3) -o $@ $<
	
clean:
	rm -f uqfindexec
