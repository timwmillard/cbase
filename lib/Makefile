CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Werror -Wpedantic

all: base_test linalg_test

# base.h
base_test: base_test.c base.h
	$(CC) $(CFLAGS) -o base_test base_test.c

# linalg.h
linalg.h: linalg_gen
	./linalg_gen > linalg.h

linalg_test.c: linalg_gen
	./linalg_gen --test > linalg_test.c

linalg_gen: linalg_gen.c
	$(CC) $(CFLAGS) -o linalg_gen linalg_gen.c

linalg_test: linalg_test.c linalg.h
	$(CC) $(CFLAGS) -o linalg_test linalg_test.c -lm

# Run tests

test: base_test linalg_test
	./base_test
	./linalg_test

# Clean

clean:
	rm -f base_test linalg_gen linalg_test linalg.h linalg_test.c


