CFLAGS=-std=c11 -g -static -fno-common

zcc: main.o
	$(CC) -o $@ $? $(LDFLAGS)

/tmp/tmpfs:
	mkdir -p /tmp/tmpfs

test: zcc /tmp/tmpfs
	./test.sh

clean:
	rm -f zcc *.o *~ tmp*
	rm -rf /tmp/tmpfs/*

.PHONY: test clean

