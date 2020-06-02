CFLAGS=-std=c11 -g -static -fno-common
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

zcc: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): zcc.h

/tmp/tmpfs:
	mkdir -p /tmp/tmpfs

zcc-stage2: zcc $(SRCS) zcc.h self.sh
	./self.sh

test: zcc tests/extern.o /tmp/tmpfs
	./zcc tests/tests.c > /tmp/tmpfs/tmp.s
	gcc -static -o /tmp/tmpfs/tmp /tmp/tmpfs/tmp.s tests/extern.o
	/tmp/tmpfs/tmp

test-stage2: zcc-stage2 tests/extern.o
	./zcc-stage2 tests/tests.c > /tmp/tmpfs/tmp.s
	gcc -static -o /tmp/tmpfs/tmp /tmp/tmpfs/tmp.s tests/extern.o
	/tmp/tmpfs/tmp

queen: zcc /tmp/tmpfs
	./zcc tests/nqueen.c > /tmp/tmpfs/tmp.s
	gcc -static -o /tmp/tmpfs/tmp /tmp/tmpfs/tmp.s
	/tmp/tmpfs/tmp

clean:
	rm -rf zcc zcc-stage* *.o *~ tmp* tests/*~ tests/*.o
	rm -rf /tmp/tmpfs/*

.PHONY: test clean

