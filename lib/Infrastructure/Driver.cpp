
#include "ASTContext.h"
#include "Driver.h"

using namespace Yorkie;

// ================================================================
// Passes
// ================================================================

void Pass::run(ASTContext context) {
    Function();
}

// ================================================================
// Driver
// ================================================================

void Driver::run(ASTContext context) {
    for (Pass pass : Passes) {
        pass.run(context);
    }
}

void Driver::add(Pass pass) {
    Passes.push_back(pass);
}
