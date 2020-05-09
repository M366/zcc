CFLAGS=-std=c11 -g -static -fno-common
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

zcc: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): zcc.h

/tmp/tmpfs:
	mkdir -p /tmp/tmpfs

test: zcc /tmp/tmpfs
	./test.sh

clean:
	rm -f zcc *.o *~ tmp*
	rm -rf /tmp/tmpfs/*

.PHONY: test clean

