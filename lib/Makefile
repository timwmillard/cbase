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
# normal builds never need the network. To regenerate:
#
#   make utf8-tables                      # pinned UCD_VERSION below
#   make utf8-tables UCD_VERSION=18.0.0   # a specific release
#   make utf8-update                      # latest release, always re-fetched
#
# Then `make test` and review `git diff --stat utf8.h`. Each version is
# downloaded into its own ucd/<version>/ directory, so changing the version
# always fetches fresh files instead of silently reusing old ones.
UCD_VERSION ?= 17.0.0
ifeq ($(UCD_VERSION),latest)
UCD_URL = https://www.unicode.org/Public/UCD/latest/ucd
else
UCD_URL = https://www.unicode.org/Public/$(UCD_VERSION)/ucd
endif
UCD_DIR = ucd/$(UCD_VERSION)
UCD_FILES = $(UCD_DIR)/UnicodeData.txt $(UCD_DIR)/CaseFolding.txt \
            $(UCD_DIR)/Scripts.txt $(UCD_DIR)/license.txt

# Use curl if installed, otherwise wget. Override with e.g. FETCH="wget -qO".
ifneq ($(shell command -v curl 2>/dev/null),)
FETCH ?= curl -sSfL -o
else
FETCH ?= wget -q -O
endif

utf8_test: utf8_test.c utf8.h base.h
	$(CC) $(CFLAGS) -o utf8_test utf8_test.c

utf8_gen: utf8_gen.c
	$(CC) $(CFLAGS) -O2 -o utf8_gen utf8_gen.c

# Download to a .tmp file and rename, so a failed fetch never leaves a
# truncated file that make would treat as up to date.
$(UCD_DIR)/%.txt:
	@mkdir -p $(@D)
	$(FETCH) $@.tmp $(UCD_URL)/$(@F) && mv $@.tmp $@

$(UCD_DIR)/license.txt:
	@mkdir -p $(@D)
	$(FETCH) $@.tmp https://www.unicode.org/license.txt && mv $@.tmp $@

utf8-tables: utf8_gen $(UCD_FILES)
	./utf8_gen $(UCD_DIR) utf8.h

utf8-update:
	rm -rf ucd/latest
	$(MAKE) utf8-tables UCD_VERSION=latest

.PHONY: utf8-tables utf8-update

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

test: base_test map_test utf8_test linalg_test net-test
	./base_test
	./map_test
	./utf8_test
	./linalg_test

# net (webc): local sockets only, about 15 s; see net_test/Makefile
net-test:
	$(MAKE) -C net_test test

.PHONY: net-test

# Clean

clean:
	rm -f base_test map_test utf8_test utf8_gen linalg_gen linalg_test linalg.h linalg_test.c
	$(MAKE) -C net_test clean


