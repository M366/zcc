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
	echo 'int ext1; int *ext2; int ext3 = 5; int char_fn() { return 257; }' \
		  'int static_fn() { return 5; }' | \
	      gcc -xc -c -fno-common -o tmp2.o -
	gcc -static -o /tmp/tmpfs/tmp /tmp/tmpfs/tmp.s tmp2.o
	/tmp/tmpfs/tmp

queen: zcc /tmp/tmpfs
	./zcc tests/nqueen.c > /tmp/tmpfs/tmp.s
	gcc -static -o /tmp/tmpfs/tmp /tmp/tmpfs/tmp.s
	/tmp/tmpfs/tmp

clean:
	rm -rf zcc *.o *~ tmp* tests/*~ tests/*.o
	rm -rf /tmp/tmpfs/*

.PHONY: test clean

