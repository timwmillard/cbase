.PHONY: all run clean

all: build
	@cmake --build build

build:
	@cmake -B build

run: all
	@./build/jobqd

clean:
	@rm -rf build
