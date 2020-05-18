CFLAGS=-std=c11 -g -static -fno-common
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

zcc: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): zcc.h

/tmp/tmpfs:
	mkdir -p /tmp/tmpfs

test: zcc /tmp/tmpfs
	./zcc tests/tests.c > /tmp/tmpfs/tmp.s
	gcc -static -o /tmp/tmpfs/tmp /tmp/tmpfs/tmp.s
	/tmp/tmpfs/tmp

clean:
	rm -rf zcc *.o *~ tmp* tests/*~ tests/*.o
	rm -rf /tmp/tmpfs/*

.PHONY: test clean

