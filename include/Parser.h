#ifndef YORKIE_PARSER_H
#define YORKIE_PARSER_H

#include <map>
#include <string>
#include <stdio.h>
#include <stdlib.h>

//===============================================
// Parser.h
//
// Take input from the Lexer and produce an AST.
//
//===============================================

// Forward declares
class ExprAST;
class FunctionAST;
class PrototypeAST;
class ASTContext;
namespace Lexer { class Lexer; }

class Parser {

private:
    // Handle binary operator precedence
    // https://en.wikipedia.org/wiki/Operator-precedence_parser
    // BinopPrecendence - This holds the precedence for each binary operator that is defined.
    std::map<char, int> BinopPrecedence;

    //TODO: Probably a better way to define these as private.
    void HandleTopLevelExpression(Lexer::Lexer &lexer);
    void HandleExtern(Lexer::Lexer &lexer);
    void HandleDefinition(Lexer::Lexer &lexer, ASTContext &context);
    int GetTokPrecedence(Lexer::Lexer &lexer);

public:

    Parser();

    void ParseTopLevel(Lexer::Lexer &lexer, ASTContext &context);

    std::shared_ptr<ExprAST> ParsePrimary(Lexer::Lexer &lexer);
    std::shared_ptr<ExprAST> ParseIndentifierExpr(Lexer::Lexer &lexer);
    std::shared_ptr<ExprAST> ParseNumberExpr(Lexer::Lexer &lexer);
    std::shared_ptr<ExprAST> ParseParenExpr(Lexer::Lexer &lexer);
    std::shared_ptr<ExprAST> ParseIfExpr(Lexer::Lexer &lexer);
    std::shared_ptr<ExprAST> ParseCompoundExpr(Lexer::Lexer &lexer);
    std::shared_ptr<ExprAST> ParseForExpr(Lexer::Lexer &lexer);
    std::shared_ptr<ExprAST> ParseVarExpr(Lexer::Lexer &lexer);
    std::shared_ptr<ExprAST> ParseExpression(Lexer::Lexer &lexer);
    std::shared_ptr<ExprAST> ParseUnary(Lexer::Lexer &lexer);
    std::shared_ptr<FunctionAST> ParseTopLevelExpr(Lexer::Lexer &lexer);
    std::shared_ptr<PrototypeAST> ParseExtern(Lexer::Lexer &lexer);
    std::shared_ptr<FunctionAST> ParseDefinition(Lexer::Lexer &lexer);
    std::shared_ptr<PrototypeAST> ParsePrototype(Lexer::Lexer &lexer);
    std::shared_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::shared_ptr<ExprAST> LHS, Lexer::Lexer &lexer);

};

#endif /* end of include guard:  */
