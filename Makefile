all: build

build:
	time clang++ -g lib/toy.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core mcjit native` -O3 -o toy

.PHONY: all build
