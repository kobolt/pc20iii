OBJECTS=main.o mem.o i8088.o i8088_trace.o io.o fe2010.o mos5720.o fdc9268.o m6242.o xthdc.o i8250.o dp8390.o net.o edfs.o console.o debugger.o
CFLAGS=-Wall -Wextra -DCPU_RELAX -DCPU_TRACE -DBREAKPOINT
LDFLAGS=-lncurses

all: pc20iii

pc20iii: ${OBJECTS}
	gcc -o pc20iii $^ ${LDFLAGS}

main.o: main.c
	gcc -c $^ ${CFLAGS}

mem.o: mem.c
	gcc -c $^ ${CFLAGS}

i8088.o: i8088.c
	gcc -c $^ ${CFLAGS}

i8088_trace.o: i8088_trace.c
	gcc -c $^ ${CFLAGS}

io.o: io.c
	gcc -c $^ ${CFLAGS}

fe2010.o: fe2010.c
	gcc -c $^ ${CFLAGS}

mos5720.o: mos5720.c
	gcc -c $^ ${CFLAGS}

fdc9268.o: fdc9268.c
	gcc -c $^ ${CFLAGS}

m6242.o: m6242.c
	gcc -c $^ ${CFLAGS}

xthdc.o: xthdc.c
	gcc -c $^ ${CFLAGS}

i8250.o: i8250.c
	gcc -c $^ ${CFLAGS}

dp8390.o: dp8390.c
	gcc -c $^ ${CFLAGS}

net.o: net.c
	gcc -c $^ ${CFLAGS}

edfs.o: edfs.c
	gcc -c $^ ${CFLAGS}

console.o: console.c
	gcc -c $^ ${CFLAGS}

debugger.o: debugger.c
	gcc -c $^ ${CFLAGS}

.PHONY: clean
clean:
	rm -f *.o pc20iii

