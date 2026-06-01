CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -Iinclude -g
SRC     = $(wildcard src/*.c)

.PHONY: all clean test

all: sangen

sangen: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

test: sangen
	./test/run_tests.sh

clean:
	rm -f sangen test/.actual_*.txt test/.*.c test/.*_c test/.invalid_*.kbn
	rm -rf sangen.dSYM
