
#include "Driver.h"

using namespace Yorkie;

// ================================================================
// Passes
// ================================================================

void Pass::run() {
    Function();
}

// ================================================================
// Driver
// ================================================================

void Driver::run() {
    for (Pass pass : Passes) {
        pass.run();
    }
}

void Driver::add(Pass pass) {
    Passes.push_back(pass);
}