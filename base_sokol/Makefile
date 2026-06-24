
.PHONY: all run

all: build
	@cmake --build build

build:
	@cmake -B build

run: all
	@./build/sokol_demo

clean:
	@rm -rf build

