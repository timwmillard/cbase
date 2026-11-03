
.PHONY: all run

all: build
	@cmake --build build

build:
	@cmake -B build

run: all
	@./build/https_client

clean:
	@rm -rf build

