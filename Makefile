CC=gcc
LD=gcc
CFLAGS=-c
LDFLAGS=

RM=rm -f

ALL=ipcq
OBJS=ipcq.o

all: $(ALL)

ipcq: ipcq.o
	$(LD) $(LDFLAGS) ipcq.o -o $(@)

ipcq.o: ipcq.c
	$(CC) $(CFLAGS) ipcq.c

realclean: clean
	-$(RM) $(ALL)

clean:
	-$(RM) $(OBJS)
