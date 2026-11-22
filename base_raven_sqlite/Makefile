
.PHONY: all run

all: build
	@cmake --build build

build:
	@cmake -B build

run: all
	@./build/app

clean:
	@rm -rf build

