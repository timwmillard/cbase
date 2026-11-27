# Tests for lib/net (webc). They live outside lib/net so that vendoring
# lib/net/** doesn't copy them into every app.
#
#   make test     the protocol tests, then http and ws against in-process
#                 servers on 127.0.0.1 (no outside network needed)
#   make tsan     the same with ThreadSanitizer
#   make asan     the same with AddressSanitizer + UBSan
#
# On macOS each network test is built twice: against Network.framework and,
# with NET_BACKEND_UNIX, against the POSIX backend.

CC     ?= cc
CFLAGS ?= -O1 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror -Wpedantic -I..
BUILD  ?= build

NET     = ../net
NET_SRC = $(addprefix $(NET)/,net_conn.c net_sys.c net_url.c ws.c ws_proto.c \
            http.c http_proto.c sse.c)
NET_DEP = $(wildcard $(NET)/*.c $(NET)/*.h)

ifeq ($(shell uname),Darwin)
NATIVE_LIBS = -framework Network -framework Security -framework CoreFoundation
UNIX_PROGS  = $(BUILD)/ws_test_unix $(BUILD)/http_test_unix
else
NATIVE_LIBS = -ldl -lpthread
UNIX_PROGS  =
endif
UNIX_LIBS = -ldl -lpthread

PROGS = $(BUILD)/net_proto_test $(BUILD)/ws_test $(BUILD)/http_test $(UNIX_PROGS)

.PHONY: all test tsan asan clean
all: $(PROGS)

$(BUILD)/net_proto_test: net_proto_test.c $(NET_DEP)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SAN) $< $(NET_SRC) -o $@ $(NATIVE_LIBS)

$(BUILD)/%_test: %_test.c $(NET_DEP)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SAN) $< $(NET_SRC) -o $@ $(NATIVE_LIBS)

$(BUILD)/%_test_unix: %_test.c $(NET_DEP)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -DNET_BACKEND_UNIX $< $(NET_SRC) -o $@ $(UNIX_LIBS)

test: $(PROGS)
	@for p in $(PROGS); do echo "== $$p"; ./$$p || exit 1; done

tsan:
	$(MAKE) BUILD=build-tsan SAN=-fsanitize=thread test

asan:
	$(MAKE) BUILD=build-asan SAN="-fsanitize=address,undefined -fno-omit-frame-pointer" test

clean:
	rm -rf build build-tsan build-asan
