OS:=$(shell uname)
ARCH:=$(shell uname -m)

HVOPT=-DHV
CFLAGS=$(HVOPT) -Wall
LDFLAGS=-lpthread -lutil

RM=rm -f
CC=gcc
OPTIMIZATION=-O2


ifeq ($(OS),Linux)
	CFLAGS += -D Linux
	CFLAGS += -D TAP

	TARGET = vpcs
endif

ifeq ($(OS),CYGWIN*)
	CFLAGS +=-D Linux
	CFLAGS +=-D i386
	CFLAGS +=-D cygwin
	CFLAGS +=-D TAP
	CFLAGS +=-I.

	LDFLAGS += -static -s -static-libgcc

	TARGET = vpcs.exe
endif

ifeq ($(OS),FreeBSD)
	CC = clang

	CFLAGS += -D FreeBSD
	CFLAGS += -D i386
	CFLAGS += -I.

	LDFLAGS += -static

	TARGET = vpcs
endif

ifeq ($(OS), Darwin)
	CFLAGS +=-D Darwin
	CFLAGS +=-D i386

	TARGET = vpcs
endif

ifeq ($(ARCH), i386)
	CFLAGS += -m32
endif
ifeq ($(ARCH), 32)
	CFLAGS += -m32
endif
ifeq ($(ARCH), amd64)
	CFLAGS += -m64
endif
ifeq ($(ARCH), x86_64)
	CFLAGS += -m64
endif
ifeq ($(ARCH), 64)
	CFLAGS += -m64
endif


SOURCEDIR = ../src
SOURCES = $(wildcard $(SOURCEDIR)/*.c)
OBJS = $(patsubst $(SOURCEDIR)/%.c,%.o,$(SOURCES))
	
all: vpcs

vpcs: $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS) $(OPTIMIZATION)

debug: OPTIMIZATION=-g
debug: vpcs

website.c: website.html
	xxd -i website.html > website.c

%.o: $(SOURCEDIR)/%.c $(SOURCEDIR)/*.h
	$(CC) $(OPTIMIZATION) $(CFLAGS) -c $<

clean:
	$(RM) $(OBJS) $(TARGET)

