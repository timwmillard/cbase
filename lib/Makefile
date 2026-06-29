
all: base_test

test: base_test
	./base_test

base_test: base_test.c base.h
	gcc -o base_test -std=c11 -Wall -Wextra -Werror -Wpedantic base_test.c

clean:
	rm -f base_test
