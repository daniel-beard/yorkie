all: stdlib build

build:
	time clang++ -g lib/toy.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core mcjit native irreader linker` -O3 -o toy

stdlib:
	time clang++ -S -emit-llvm lib/stdlib.cpp -o lib/stdlib.ll

fib:
	./toy < examples/fib.yk 2>&1 | clang -x ir -

.PHONY: all build fib stdlib
