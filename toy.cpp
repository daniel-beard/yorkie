
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <iostream>
#include <memory>

using namespace llvm;

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
    virtual Value *codegen() = 0;
};

// NumberExprAST - Expression class for numeric literals like "1.0"
class NumberExprAST: public ExprAST {
    double Val;

public:
    NumberExprAST(double Val) : Val(Val) {}
    Value *codegen() override;
};

// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
    std::string Name;

public:
    VariableExprAST(const std::string &Name) : Name(Name) {};
    Value *codegen() override;
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
    Value *codegen() override;
};

// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST> > Args;

public:
    CallExprAST(const std::string &Callee,
            std::vector<std::unique_ptr<ExprAST> > Args) :
        Callee(Callee), Args(std::move(Args)) {}
    Value *codegen() override;
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
    Function *codegen();
    const std::string &getName() const { return Name; }
};

// FunctionAST - This class represents a function definition itself.
class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

public:
    FunctionAST(std::unique_ptr<PrototypeAST> Proto,
            std::unique_ptr<ExprAST> Body) :
        Proto(std::move(Proto)), Body(std::move(Body)) {}
    Function *codegen();
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
    auto Result = llvm::make_unique<NumberExprAST>(NumVal);
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
        return llvm::make_unique<VariableExprAST>(IdName);

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
            getNextToken(); // eat the ','
        }
    }

    // Eat the ')'.
    getNextToken();

    return llvm::make_unique<CallExprAST>(IdName, std::move(Args));
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
        LHS = llvm::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
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

    return llvm::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

// Function definition, just a prototype plus an expression to implement the body
// definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
    getNextToken(); // eat def.
    auto Proto = ParsePrototype();
    if (!Proto) return nullptr;

    if (auto E = ParseExpression())
        return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(E));
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
        // Make anonymous proto
        auto Proto = llvm::make_unique<PrototypeAST>("", std::vector<std::string>());
        return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

// ================================================================
// Code Generation
// ================================================================

// Used during code generation
// TheModule is an LLVM construct that contains functions and global variables.
// It owns the memory for all of the IR we generate. (Also why codegen() returns raw Value* rather than unique_ptr(Value)
// Builder is a helper object that makes it easy to generate LLVM instructions.
// NamedValues keeps track of which values are defined in the current scope, 
// and what their LLVM representation is.
static std::unique_ptr<Module> TheModule;
static IRBuilder<> Builder(getGlobalContext());
static std::map<std::string, Value*> NamedValues;

Value *ErrorV(const char *Str) {
    Error(Str);
    return nullptr;
}

// Generate code for numeric literals
// `APFloat` has the capability of holder fp constants of arbitrary precision.
Value *NumberExprAST::codegen() {
    return ConstantFP::get(getGlobalContext(), APFloat(Val));
}

// Generate code for variable expressions
Value *VariableExprAST::codegen() {
    // Look this variable up in the function
    Value *V = NamedValues[Name];
    if (!V) 
        ErrorV("Unknown variable name");
    return V;
}

// Generate code for binary expressions
// Recursively emit code for the LHS then the RHS then compute the result.
// LLVM instructions have strict rules, e.g. add - LHS and RHS must have the same type.
// fcmp always returns an 'i1' value (a one bit integer).
// We want 0.0 or 1.0 for this, so we use a uitofp instruction.
Value *BinaryExprAST::codegen() {
    Value *L = LHS->codegen();
    Value *R = RHS->codegen();

    if (!L || !R)
        return nullptr;

    switch (Op) {
    case '+':
        return Builder.CreateFAdd(L, R, "addtmp");
    case '-':
        return Builder.CreateFSub(L, R, "subtmp");
    case '*':
        return Builder.CreateFMul(L, R, "multmp");
    case '<':
        L = Builder.CreateFCmpULT(L, R, "cmptmp");
        // Convert bool 0/1 to double 0.0 of 1.0
        return Builder.CreateUIToFP(L, Type::getDoubleTy(getGlobalContext()), "booltmp");
    default:
        return ErrorV("invalid binary operator");
    }
}

// Generate code for function calls
// Lookup function name in the LLVM Module's symbol table.
// We use the same name in the symbol table as what the user specifies.
// Note that LLVM uses the native C calling conventions by default,
// allowing these calls to also call into standard lib functions like `sin` and `cos`.
Value *CallExprAST::codegen() {
    // Look up the name in the global module table
    Function *CalleeF = TheModule->getFunction(Callee);
    if (!CalleeF)
        return ErrorV("Unknown function referenced");

    // If argument mismatch error
    if (CalleeF->arg_size() != Args.size())
        return ErrorV("Incorrect # arguments passed");

    std::vector<Value *> ArgsV;
    for (unsigned i = 0, e = Args.size(); i != e; ++i) {
        ArgsV.push_back(Args[i]->codegen());
        if (!ArgsV.back())
            return nullptr;
    }
    return Builder.CreateCall(CalleeF, ArgsV, "calltmp");
}

// Generate code for function declarations (prototypes)
// All function types are Doubles for now
Function *PrototypeAST::codegen() {
    // Make the function type: double(double, double) etc.
    std::vector<Type*> Doubles(Args.size(), 
            Type::getDoubleTy(getGlobalContext()));
    
    // false specifies this is not a vargs function
    FunctionType *FT = FunctionType::get(Type::getDoubleTy(getGlobalContext()), Doubles, false);
    // ExternalLinkage means function may be defined outside the current module
    // or that it is callable by functions outside the module.
    Function *F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

    // Set names for all arguments.
    unsigned Idx = 0;
    for (auto &Arg : F->args())
        Arg.setName(Args[Idx++]);

    return F;
}

// Generate code for function bodies.
// TODO: Fix bug listed at the end of this section: http://llvm.org/docs/tutorial/LangImpl3.html#function-code-generation
Function *FunctionAST::codegen() {
    // First, check for an existing function from a previous 'extern' declaration.
    Function *TheFunction = TheModule->getFunction(Proto->getName());

    // Otherwise, codegen from the prototype
    if (!TheFunction)
        TheFunction = Proto->codegen();

    if (!TheFunction)
        return nullptr;
    
    // Want to make sure that the function doesn't already have a body before we generate one.
    if (!TheFunction->empty())
        return (Function*)ErrorV("Function cannot be redefined.");

    // Create a new basic block to start insertion into.
    BasicBlock *BB = BasicBlock::Create(getGlobalContext(), "entry", TheFunction);
    Builder.SetInsertPoint(BB);

    // Record the function arguments in the NamedValues map.
    // Add the function arguments to the NamedValues map, so they are accessible to the 
    // `VariableExprAST` nodes
    NamedValues.clear();
    for (auto &Arg : TheFunction->args())
        NamedValues[Arg.getName()] = &Arg;

    // If no error, emit the ret instruction, which completes the function.
    if (Value *RetVal = Body->codegen()) {
        // Finish off the function.
        Builder.CreateRet(RetVal);

        // Validate the generated code, checking for consistency. Function is provided by LLVM.
        verifyFunction(*TheFunction);

        return TheFunction;
    }

    // Error reading body, remove function.
    TheFunction->eraseFromParent();
    return nullptr;
}



// ================================================================
// Top-Level parsing and JIT Driver
// ================================================================

static void HandleDefinition() {
    if (auto FnAST = ParseDefinition()) {
        if (auto *FnIR = FnAST->codegen()) {
            fprintf(stderr, "Read function definition:");
            FnIR->dump();
        }
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleExtern() {
    if (auto ProtoAST = ParseExtern()) {
        if (auto *FnIR = ProtoAST->codegen()) {
            fprintf(stderr, "Parsed an extern\n");
            FnIR->dump();
        }
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleTopLevelExpression() {
    // Evaluate a top-level expression into an anonymous function.
    if (auto FnAST = ParseTopLevelExpr()) {
        if (auto *FnIR = FnAST->codegen()) {
            fprintf(stderr, "Parsed a top-level expr\n");
            FnIR->dump();
        }
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

    // Make the module, which holds all the code
    TheModule = llvm::make_unique<Module>("dbeard jit", getGlobalContext());

    // Run the main "interpreter loop" now.
    MainLoop();

    // Print out all of the generated code
    TheModule->dump();

    return 0;
}






