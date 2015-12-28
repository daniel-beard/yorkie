#!/bin/bash

set -ev

LLVM_TAR="clang+llvm-3.7.0-x86_64-linux-gnu-ubuntu-14.04.tar.xz"

curl -o $LLVM_TAR http://llvm.org/releases/3.7.0/$LLVM_TAR \
    && tar xf $LLVM_TAR \
    && echo "Directory: " && ls
