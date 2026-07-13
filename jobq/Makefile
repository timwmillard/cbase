CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O2 -g -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
PREFIX  ?= /usr/local

LUA_PKG ?= $(shell pkg-config --exists lua5.4 && echo lua5.4 || \
             (pkg-config --exists lua5.3 && echo lua5.3 || echo lua))

CFLAGS  += -fPIC $(shell pkg-config --cflags $(LUA_PKG) sqlite3)
LDLIBS  += $(shell pkg-config --libs $(LUA_PKG) sqlite3) -lpthread

LIB_SRC = src/queue.c src/pool.c
LIB_OBJ = $(LIB_SRC:.c=.o)
HDRS    = src/jobq.h src/queue.h src/pool.h

all: libjobq.a libjobq.so jobqd

# --- library -----------------------------------------------------------
libjobq.a: $(LIB_OBJ)
	$(AR) rcs $@ $^

libjobq.so: $(LIB_OBJ)
	$(CC) -shared -o $@ $^ $(LDLIBS)

# --- standalone daemon/CLI ----------------------------------------------
jobqd: src/main.o libjobq.a
	$(CC) $(CFLAGS) -o $@ src/main.o libjobq.a $(LDLIBS)

src/%.o: src/%.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- install -------------------------------------------------------------
install: all
	install -d $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include/jobq $(DESTDIR)$(PREFIX)/bin
	install -m 644 libjobq.a libjobq.so $(DESTDIR)$(PREFIX)/lib/
	install -m 644 $(HDRS) $(DESTDIR)$(PREFIX)/include/jobq/
	install -m 755 jobqd $(DESTDIR)$(PREFIX)/bin/

clean:
	rm -f jobqd libjobq.a libjobq.so src/*.o jobs.db jobs.db-wal jobs.db-shm

.PHONY: all install clean
