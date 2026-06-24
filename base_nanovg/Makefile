
.PHONY: all run

all: build
	@cmake --build build

build:
	@cmake -B build

run: all
	@./build/nanovg_demo

clean:
	@rm -rf build

