
.PHONY: all run

NPROC ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

all: build
	@cmake --build build --parallel $(NPROC)

build:
	@cmake -B build

run: all
	@./build/nuklear_demo

clean:
	@rm -rf build

