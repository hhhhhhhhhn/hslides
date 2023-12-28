CFLAGS = -Wall -Werror -Wextra -Wpedantic -std=c99 -g

ifdef debug
	CFLAGS += -DHLIB_DEBUG
endif

all: hslides

hslides: hslides.c hlib.o
	cc $(CFLAGS) -o hslides hslides.c hlib.o

hlib.o: $(wildcard hlib/*.c)
	cc $(CFLAGS) -c hlib/hlib.c -o hlib.o

clean:
	rm -f *.o *_test
