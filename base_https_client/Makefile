
.PHONY: all run

all: build
	@cmake --build build

build:
	@cmake -B build

run: all
	@./build/https_client www.google.com 80

clean:
	@rm -rf build

