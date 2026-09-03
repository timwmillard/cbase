
.PHONY: all run

all: build
	@cmake --build build

build:
	@cmake -B build

run: all
	@./build/base_sdl3

clean:
	@rm -rf build

