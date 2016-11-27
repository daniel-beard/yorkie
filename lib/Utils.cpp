#include "Utils.h"
#include "Lexer.h"
#include "AST.h"

// =============================================================================
// Error* - These are little helper functions for error handling.
// =============================================================================

std::unique_ptr<ExprAST> Error(const char *Str, Lexer::Lexer &lexer) {
    fprintf(stderr, "Error: %s Location: %d:%d\n", Str, lexer.getLexLoc().Line, lexer.getLexLoc().Col);
    return nullptr;
}
std::unique_ptr<PrototypeAST> ErrorP(const char *Str, Lexer::Lexer &lexer) {
    Error(Str, lexer);
    return nullptr;
}
std::unique_ptr<FunctionAST> ErrorF(const char *Str, Lexer::Lexer &lexer) {
    Error(Str, lexer);
    return nullptr;
}

// Simple errors (no line numbers)

std::unique_ptr<ExprAST> Error(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}

llvm::Value *ErrorV(const char *Str) {
    Error(Str);
    return nullptr;
}
