#ifndef YORKIE_UTILS_H
#define YORKIE_UTILS_H

//===============================================
// Utils.h
//
// Utility functions used across classes.
//
//===============================================

#include "AST.h"
#include "llvm/IR/Value.h"
#include <memory>
#include "Lexer.h"

std::unique_ptr<ExprAST> Error(const char *Str, Lexer::Lexer &lexer);
std::unique_ptr<PrototypeAST> ErrorP(const char *Str, Lexer::Lexer &lexer);
std::unique_ptr<FunctionAST> ErrorF(const char *Str, Lexer::Lexer &lexer);

// Simple Errors (no line numbers)
std::unique_ptr<ExprAST> Error(const char *Str);
llvm::Value *ErrorV(const char *Str);

#endif
