#ifndef YORKIE_UTILS_H
#define YORKIE_UTILS_H

//===============================================
// Utils.h
//
// Utility functions used across classes.
//
//===============================================

#include "llvm/IR/Value.h"
#include <memory>

class ExprAST;
class PrototypeAST;
class FunctionAST;
namespace Lexer { class Lexer; }

std::shared_ptr<ExprAST> Error(const char *Str, Lexer::Lexer &lexer);
std::shared_ptr<PrototypeAST> ErrorP(const char *Str, Lexer::Lexer &lexer);
std::shared_ptr<FunctionAST> ErrorF(const char *Str, Lexer::Lexer &lexer);

// Simple Errors (no line numbers)
std::shared_ptr<ExprAST> Error(const char *Str);
llvm::Value *ErrorV(const char *Str);

#endif
