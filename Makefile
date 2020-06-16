CFLAGS=-std=c11 -g -fno-common
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)
TMPFS=/tmp/tmpfs

zcc: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): zcc.h

$(TMPFS):
	mkdir -p $(TMPFS)

zcc-stage2: zcc $(SRCS) zcc.h self.sh
	./self.sh tmp-stage2 ./zcc zcc-stage2

zcc-stage3: zcc-stage2
	./self.sh tmp-stage3 ./zcc-stage2 zcc-stage3

test: zcc tests/extern.o $(TMPFS)
	(cd tests; ../zcc -I. -DANSWER=42 tests.c) > $(TMPFS)/tmp.s
	gcc -o $(TMPFS)/tmp $(TMPFS)/tmp.s tests/extern.o
	$(TMPFS)/tmp

test-nopic: zcc tests/extern.o $(TMPFS)
	(cd tests; ../zcc -fno-pic -I. -DANSWER=42 tests.c) > $(TMPFS)/tmp.s
	gcc -static -o $(TMPFS)/tmp $(TMPFS)/tmp.s tests/extern.o
	$(TMPFS)/tmp

test-stage2: zcc-stage2 tests/extern.o
	(cd tests; ../zcc-stage2 -I. -DANSWER=42 tests.c) > $(TMPFS)/tmp.s
	gcc -o $(TMPFS)/tmp $(TMPFS)/tmp.s tests/extern.o
	$(TMPFS)/tmp

test-stage3: zcc-stage3
	diff zcc-stage2 zcc-stage3

test-all: test test-nopic test-stage2 test-stage3

queen: zcc $(TMPFS)
	./zcc tests/nqueen.c > $(TMPFS)/tmp.s
	gcc -o $(TMPFS)/tmp $(TMPFS)/tmp.s
	$(TMPFS)/tmp

uqueen: zcc $(TMPFS)
	./zcc tests/nqueen_utf8.c > $(TMPFS)/tmp.s
	gcc -o $(TMPFS)/tmp $(TMPFS)/tmp.s
	$(TMPFS)/tmp

clean:
	rm -rf zcc zcc-stage* *.o *~ tmp* tests/*~ tests/*.o
	rm -rf $(TMPFS)/*

.PHONY: test clean

