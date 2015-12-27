
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <iostream>
#include <memory>

namespace helper {
    // Cloning make_unique here until it's standard in C++14.
    // Using a namespace to avoid conflicting with MSVC's std::make_unique (which
    // ADL can sometimes find in unqualified calls).
template <class T, class... Args>
 static
    typename std::enable_if<!std::is_array<T>::value, std::unique_ptr<T>>::type
    make_unique(Args &&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
}

// http://llvm.org/docs/tutorial/LangImpl1.html#the-lexer
// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
    tok_eof = -1,

    // commands
    tok_def = -2,
    tok_extern = -3,

    // primary
    tok_identifier = -4,
    tok_number = -5,
};

static std::string IdentifierStr;   // Filled in if tok_identifier
static double NumVal;               // Filled in if tok_number

/// gettok - Return the next token from standard input.
static int gettok() {
    static int LastChar = ' ';

    // Skip any whitespace
    while (isspace(LastChar)) {
        LastChar = getchar();
    }

    // Recognize identifiers and specific keywords like 'def'
    if (isalpha(LastChar)) { // identifier: [a-zA-Z][a-zA-Z0-9]*
        IdentifierStr = LastChar;
        while (isalnum((LastChar = getchar()))) {
            IdentifierStr += LastChar;
        }

        if (IdentifierStr == "def")
            return tok_def;
        if (IdentifierStr == "extern")
            return tok_extern;

        return tok_identifier;
    }

    // Handle Numeric Values
    // Naive, won't handle things like 1.1.1 etc.
    if (isdigit(LastChar) || LastChar == '.') { // Number: [0-9.]+
        std::string NumStr;
        do {
            NumStr += LastChar;
            LastChar = getchar();
        } while (isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(), 0);
        return tok_number;
    }

    // Handle comments
    // We skip to the end of the line then return the next token
    if (LastChar == '#') {
        // Comment until end of line.
        do {
            LastChar = getchar();
        } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar != EOF) {
            return gettok();
        }
    }

    // If the input doesn't match one of the above cases, it is either an operator
    // character like '+' or the end of file. Handle these below.
    
    // Check for end of file. Don't eat the EOF
    if (LastChar == EOF) {
        return tok_eof;
    }

    // Otherwise, just return the character as its ascii value.
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
}

// ================================================================
// AST
// ================================================================

// For now we are going to have expressions, prototypes and a function AST object.

// ExprAST - Base class for all expression nodes.
class ExprAST {
public:
    virtual ~ExprAST() {}
};

// NumberExprAST - Expression class for numeric literals like "1.0"
class NumberExprAST: public ExprAST {
    double Val;

public:
    NumberExprAST(double Val) : Val(Val) {}
};

// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
    std::string Name;

public:
    VariableExprAST(const std::string &Name) : Name(Name) {};
};

// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;

public:
    BinaryExprAST(char op,
            std::unique_ptr<ExprAST> LHS,
            std::unique_ptr<ExprAST> RHS) : 
        Op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};

// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST> > Args;

public:
    CallExprAST(const std::string &Callee,
            std::vector<std::unique_ptr<ExprAST> > Args) :
        Callee(Callee), Args(std::move(Args)) {}
};

// PrototypeAST - This class represents the "prototype" for a function
// which captures its name, and its argument names (this implicitly the number
// of arguments the function takes).
class PrototypeAST {
    std::string Name;
    std::vector<std::string> Args;

public:
    PrototypeAST(const std::string &name, std::vector<std::string> Args) :
        Name(name), Args(std::move(Args)) {};
};

// FunctionAST - This class represents a function definition itself.
class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

public:
    FunctionAST(std::unique_ptr<PrototypeAST> Proto,
            std::unique_ptr<ExprAST> Body) :
        Proto(std::move(Proto)), Body(std::move(Body)) {}
};


// ================================================================
// Parser
// ================================================================

// Method definitions
static std::unique_ptr<ExprAST> ParsePrimary();
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS);

// CurTok/getNextToken - Provide a simple token buffer. CurTok is the current
// token the parser is looking at. getNextToken reads another token from the lexer
// and updates CurTok with its results.
static int CurTok;
// Allows us to look one token ahead at what the lexer is returning.
static int getNextToken() {
    return CurTok = gettok();
}

// Error* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> Error(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}
std::unique_ptr<PrototypeAST> ErrorP(const char *Str) {
    Error(Str);
    return nullptr;
}

// Expression parsing

// To start, an expression is a primary expression potentially followed by a sequence of
// [binop, primaryexpr] pairs.
// expression
//  ::= primary binoprhs
static std::unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParsePrimary();
    if (!LHS)
        return nullptr;

    return ParseBinOpRHS(0, std::move(LHS));
}

// Numeric literals
// Expects to be called when the current token is a `tok_number`
// Takes the current number and creates a `NumberExprAST` node, advances to the next token
// and returns.
// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = helper::make_unique<NumberExprAST>(NumVal);
    getNextToken(); // consume the number
    return std::move(Result);
}

// Parenthesis Operator
// Demonstrates error routines, expects that the current token is `(`, but there may not be 
// a corresponding `)` token.
// We return null on an error.
// We recursively call ParseExpression, this is powerful because we can handle recursive grammars.
// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
    getNextToken(); // eat (.
    auto V = ParseExpression();
    if (!V)
        return nullptr;

    if (CurTok != ')')
        return Error("expected ')'");
    getNextToken(); // eat ).
    return V;
}

// Variable references and function calls
// Expects to be called if the current token is `tok_identifier`
// Uses look-ahead to determine if the current identifier is a stand alone var reference
// or if it is a function call expression.
// identifierexpr
//  ::= identifier
//  ::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIndentifierExpr() {
    std::string IdName = IdentifierStr;

    getNextToken(); // eat identifier

    if (CurTok != '(') // Simple variable ref
        return helper::make_unique<VariableExprAST>(IdName);

    // Call.
    getNextToken(); // Eat (
    std::vector<std::unique_ptr<ExprAST>> Args;
    if (CurTok != ')') {
        while (1) {
            if (auto Arg = ParseExpression()) {
                Args.push_back(std::move(Arg));
            } else {
                return nullptr;
            }

            if (CurTok == ')') 
                break;

            if (CurTok != ',')
                return Error("Expected ')' or ',' in argument list");
            getNextToken();
        }
    }

    // Eat the ')'.
    getNextToken();

    return helper::make_unique<CallExprAST>(IdName, std::move(Args));
}

// primary
//  ::= identifierexpr
//  ::= numberexpr
//  ::= parenexpr
static std::unique_ptr<ExprAST> ParsePrimary() {
    switch (CurTok) {
        default:
            return Error("unknown token when expecting an expression");
        case tok_identifier:
            return ParseIndentifierExpr();
        case tok_number:
            return ParseNumberExpr();
        case '(':
            return ParseParenExpr();
    }
}

// Handle binary operator precedence
// https://en.wikipedia.org/wiki/Operator-precedence_parser

// BinopPrecendence - This holds the precendence for each binary operator that is defined.
static std::map<char, int> BinopPrecedence;

// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
    if (!isascii(CurTok))
        return -1;

    // Make sure it's a declared binop.
    int TokPrec = BinopPrecedence[CurTok];
    if (TokPrec <= 0) return -1;
    return TokPrec;
}

// Parse sequences of pairs. Takes a precendence and a pointer to an expression for the 
// part that has been parsed so far.
// The precendence passed into `ParseBinOpRHS` indicates the minimal operator precendence that 
// the function is allowed to eat.
// binoprhs
//  ::= ('+' primary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS) {
    // if this is a binop, find its precendence
    while (1) {
        int TokPrec = GetTokPrecedence();

        // If this is a binop that binds at least as tightly as the current binop,
        // consume it, otherwise we are done.
        if (TokPrec < ExprPrec) 
            return LHS;

        // Okay, we know this is a binop.
        int BinOp = CurTok;
        getNextToken(); // eat binop

        // Parse the primary expression after the binary operator
        auto RHS = ParsePrimary();
        if (!RHS)
            return nullptr;

        // If BinOp binds less tightly with RHS than the operator after RHS, let
        // the pending operator take RHS as its LHS
        int NextPrec = GetTokPrecedence();
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec+1, std::move(RHS));
            if (!RHS)
                return nullptr;
        }
        // Merge LHS/RHS
        LHS = helper::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
    } // loop around to the top of the while loop
}

// Handle function prototypes, used for 'extern' function declarations as well as function
// body definitions.
// prototype
//  ::= id '(' id* ')'
static std::unique_ptr<PrototypeAST> ParsePrototype() {
    if (CurTok != tok_identifier) 
        return ErrorP("Expected function name in prototype");

    std::string FnName = IdentifierStr;
    getNextToken(); // eat identifier

    if (CurTok != '(')
        return ErrorP("Expected '(' in prototype");

    // Read list of argument names
    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier) {
        ArgNames.push_back(IdentifierStr);
    }
    if (CurTok != ')')
        return ErrorP("Expected ')' in prototype");

    // success
    getNextToken(); // eat ')'

    return helper::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

// Function definition, just a prototype plus an expression to implement the body
// definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
    getNextToken(); // eat def.
    auto Proto = ParsePrototype();
    if (!Proto) return nullptr;

    if (auto E = ParseExpression())
        return helper::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    return nullptr;
}

// Support extern to declare functions like 'sin' and 'cos' as well as to support
// forward declarations of user functions. These are just prototypes with no body.
// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
    getNextToken(); // eat extern.
    return ParsePrototype();
}

// Arbitrary top level expressions and evaluate on the fly.
// Will handle this by defining anonymous nullary (zero argument) functions for them
// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    if (auto E = ParseExpression()) {
        // Make anonymouse proto
        auto Proto = helper::make_unique<PrototypeAST>("", std::vector<std::string>());
        return helper::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

// ================================================================
// Top-Level parsing
// ================================================================

static void HandleDefinition() {
    if (ParseDefinition()) {
        fprintf(stderr, "Parsed a function definition.\n");
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleExtern() {
    if (ParseExtern()) {
        fprintf(stderr, "Parsed an extern\n");
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleTopLevelExpression() {
    // Evaluate a top-level expression into an anonymous function.
    if (ParseTopLevelExpr()) {
        fprintf(stderr, "Parsed a top-level expr\n");
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

// Driver invokes all of the parsing pieces with a top-level dispatch loop.
// Ignore top level semicolons. 
// - Reason for this is so the parser knows whether that is the end of what you will type
// at the command line.
// - E.g. allows you to type 4+5; and the parser will know you are done.
// top ::= definition | external | expression | ';'
static void MainLoop() {
    while (1) {
        fprintf(stderr, "ready> ");
        switch(CurTok) {
        case tok_eof:
            return;
        case ';': // ignore top-level semicolons.
            getNextToken();
            break;
        case tok_def:
            HandleDefinition();
            break;
        case tok_extern:
            HandleExtern();
            break;
        default:
            HandleTopLevelExpression();
            break;
        }
    }
}

// ================================================================
// Main Driver code.
// ================================================================

int main() {
    // Install standard binary operators
    // 1 is lowest precendence
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 30;
    BinopPrecedence['*'] = 40; // Highest

    // Prime the first token.
    fprintf(stderr, "ready> ");
    getNextToken();

    // Run the main "interpreter loop" now.
    MainLoop();

    return 0;
}






