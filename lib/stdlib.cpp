
#include <stdio.h>

// This file allows functions to be written in C and used from yorkie.

extern "C" double putchard(double x) {
    fputc((char)x, stderr);
    return 0;
}
