CXX=clang++
FLAGS=-std=c++11 -stdlib=libc++ -g -O3
all:

build:
	$(CXX) $(FLAGS) toy.cpp

.PHONY: all build
