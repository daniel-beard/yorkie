#include "Utils.h"

// =============================================================================
// Error* - These are little helper functions for error handling.
// =============================================================================

std::unique_ptr<ExprAST> Error(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}
std::unique_ptr<PrototypeAST> ErrorP(const char *Str) {
    Error(Str);
    return nullptr;
}
std::unique_ptr<FunctionAST> ErrorF(const char *Str) {
    Error(Str);
    return nullptr;
}

llvm::Value *ErrorV(const char *Str) {
    Error(Str);
    return nullptr;
}
