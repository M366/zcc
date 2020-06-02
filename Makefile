CFLAGS=-std=c11 -g -static -fno-common
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)
TMPFS=/tmp/tmpfs

zcc: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): zcc.h

$(TMPFS):
	mkdir -p $(TMPFS)

zcc-stage2: zcc $(SRCS) zcc.h self.sh
	./self.sh

test: zcc tests/extern.o $(TMPFS)
	./zcc tests/tests.c > $(TMPFS)/tmp.s
	gcc -static -o $(TMPFS)/tmp $(TMPFS)/tmp.s tests/extern.o
	$(TMPFS)/tmp

test-stage2: zcc-stage2 tests/extern.o
	./zcc-stage2 tests/tests.c > $(TMPFS)/tmp.s
	gcc -static -o $(TMPFS)/tmp $(TMPFS)/tmp.s tests/extern.o
	$(TMPFS)/tmp

queen: zcc $(TMPFS)
	./zcc tests/nqueen.c > $(TMPFS)/tmp.s
	gcc -static -o $(TMPFS)/tmp $(TMPFS)/tmp.s
	$(TMPFS)/tmp

clean:
	rm -rf zcc zcc-stage* *.o *~ tmp* tests/*~ tests/*.o
	rm -rf $(TMPFS)/*

.PHONY: test clean

