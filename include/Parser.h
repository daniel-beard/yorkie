#ifndef YORKIE_PARSER_H
#define YORKIE_PARSER_H

#include <map>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include "Lexer.h"

//===============================================
// Parser.h
//
// Take input from the Lexer and produce an AST.
//
//===============================================

// Forward declare AST Classes
class ExprAST;
class FunctionAST;
class PrototypeAST;

namespace Parser {

    // Handle binary operator precedence
    // https://en.wikipedia.org/wiki/Operator-precedence_parser

    // BinopPrecendence - This holds the precendence for each binary operator that is defined.
    static std::map<char, int> BinopPrecedence;

    std::unique_ptr<ExprAST> ParsePrimary(Lexer::Lexer &lexer);
    std::unique_ptr<ExprAST> ParseIndentifierExpr(Lexer::Lexer &lexer);
    std::unique_ptr<ExprAST> ParseNumberExpr(Lexer::Lexer &lexer);
    std::unique_ptr<ExprAST> ParseParenExpr(Lexer::Lexer &lexer);
    std::unique_ptr<ExprAST> ParseIfExpr(Lexer::Lexer &lexer);
    std::unique_ptr<ExprAST> ParseForExpr(Lexer::Lexer &lexer);
    std::unique_ptr<ExprAST> ParseVarExpr(Lexer::Lexer &lexer);
    std::unique_ptr<ExprAST> ParseExpression(Lexer::Lexer &lexer);
    std::unique_ptr<ExprAST> ParseUnary(Lexer::Lexer &lexer);
    std::unique_ptr<FunctionAST> ParseTopLevelExpr(Lexer::Lexer &lexer);
    std::unique_ptr<PrototypeAST> ParseExtern(Lexer::Lexer &lexer);
    std::unique_ptr<FunctionAST> ParseDefinition(Lexer::Lexer &lexer);
    std::unique_ptr<PrototypeAST> ParsePrototype(Lexer::Lexer &lexer);
    std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS, Lexer::Lexer &lexer);

}

#endif /* end of include guard:  */
