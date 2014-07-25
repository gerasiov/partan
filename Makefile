CFLAGS=-std=c99 -g -Wall

ifdef CROSS

CFLAGS+=--sysroot=$(CROSS)/sysroot
CC=$(CROSS)/bin/arm-linux-androideabi-gcc
endif

partan: partan.c

clean:
	rm partan

all: partan
