#!/bin/bash

set -ev

LLVM_VERSION="3.8.0"
LLVM_FOLDER="clang+llvm-${LLVM_VERSION}-x86_64-linux-gnu-ubuntu-14.04"
LLVM_TAR="${LLVM_FOLDER}.tar.xz"

# Install cmake 3.5
CMAKE_FOLDER="cmake-3.5.0-Linux-x86_64"
curl -o cmake.tar.gz https://cmake.org/files/v3.5/"$CMAKE_FOLDER".tar.gz
tar xf cmake.tar.gz && mv "$CMAKE_FOLDER" cmake

# Install Clang + LLVM
curl -o $LLVM_TAR http://llvm.org/releases/${LLVM_VERSION}/${LLVM_TAR} \
    && tar xf $LLVM_TAR \
    && mv $LLVM_FOLDER llvm
