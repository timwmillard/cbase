
.PHONY: all run

all: build
	@cmake --build build

build:
	@cmake -B build

run: all
	@./build/raylib_demo

clean:
	@rm -rf build

