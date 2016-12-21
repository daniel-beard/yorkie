#!/bin/bash

set -ex

# http://releases.llvm.org/3.8.0/clang+llvm-3.8.0-x86_64-apple-darwin.tar.xz
LLVM_VERSION="3.8.0"
LLVM_FOLDER="clang+llvm-${LLVM_VERSION}-x86_64-apple-darwin"
LLVM_TAR="${LLVM_FOLDER}.tar.xz"

# Install Clang + LLVM
curl -o $LLVM_TAR http://releases.llvm.org/${LLVM_VERSION}/${LLVM_TAR} \
    && tar xf $LLVM_TAR \
    && mv $LLVM_FOLDER llvm
