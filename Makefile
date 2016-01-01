all: build

build:
	time clang++ -g lib/toy.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core mcjit native` -O3 -o toy

fib:
	./toy < examples/fib.yk 2>&1 | clang -x ir -

.PHONY: all build fib
