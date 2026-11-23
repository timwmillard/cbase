CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Werror -Wpedantic

all: base_test map_test utf8_test linalg_test

# base.h
base_test: base_test.c base.h
	$(CC) $(CFLAGS) -o base_test base_test.c

# map.h
map_test: map_test.c map.h
	$(CC) $(CFLAGS) -o map_test map_test.c

# utf8.h — the data tables inside utf8.h are generated and committed, so
# normal builds never need the network. To move to a new Unicode version,
# bump UCD_VERSION and run `make utf8-tables` (downloads into ucd/).
UCD_VERSION = 17.0.0
UCD_URL = https://www.unicode.org/Public/$(UCD_VERSION)/ucd
UCD_FILES = ucd/UnicodeData.txt ucd/CaseFolding.txt ucd/Scripts.txt

utf8_test: utf8_test.c utf8.h base.h
	$(CC) $(CFLAGS) -o utf8_test utf8_test.c

utf8_gen: utf8_gen.c
	$(CC) $(CFLAGS) -O2 -o utf8_gen utf8_gen.c

$(UCD_FILES):
	@mkdir -p ucd
	curl -sSf -o $@ $(UCD_URL)/$(notdir $@)

ucd/license.txt:
	@mkdir -p ucd
	curl -sSf -o $@ https://www.unicode.org/license.txt

utf8-tables: utf8_gen $(UCD_FILES) ucd/license.txt
	./utf8_gen ucd utf8.h

.PHONY: utf8-tables

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

test: base_test map_test utf8_test linalg_test
	./base_test
	./map_test
	./utf8_test
	./linalg_test

# Clean

clean:
	rm -f base_test map_test utf8_test utf8_gen linalg_gen linalg_test linalg.h linalg_test.c


