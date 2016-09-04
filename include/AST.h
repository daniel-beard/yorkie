
#ifndef YORKIE_AST_H
#define YORKIE_AST_H

#include <iostream>
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "Lexer.h"
//===============================================
// AST.h
//
//===============================================

// ExprAST - Base class for all expression nodes.
class ExprAST {
    Lexer::SourceLocation Loc;

public:
    ExprAST(Lexer::SourceLocation Loc) : Loc(Loc) {}
    virtual ~ExprAST() {}
    virtual llvm::Value *codegen() = 0;
    int getLine() const { return Loc.Line; }
    int getCol() const { return Loc.Col; }
    virtual llvm::raw_ostream &dump(llvm::raw_ostream &out, int ind) {
        return out << ':' << getLine() << ':' << getCol() << '\n';
    }
};

// CompoundExprAST - Multiple expressions, used within FunctionAST or IfExprAST
class CompoundExprAST: public ExprAST {
    std::unique_ptr<std::vector<std::unique_ptr<ExprAST>>> Body;

public:
    CompoundExprAST(Lexer::SourceLocation Loc, std::unique_ptr<std::vector<std::unique_ptr<ExprAST>>> Body) : ExprAST(Loc), Body(std::move(Body)) {};
};

// NumberExprAST - Expression class for numeric literals like "1.0"
class NumberExprAST: public ExprAST {
    double Val;

public:
    NumberExprAST(Lexer::SourceLocation Loc, double Val) : ExprAST(Loc), Val(Val) {}
    llvm::Value *codegen() override;
};

// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
    std::string Name;

public:
    VariableExprAST(Lexer::SourceLocation Loc, const std::string &Name) : ExprAST(Loc), Name(Name) {};
    const std::string &getName() const { return Name; }
    llvm::Value *codegen() override;
};

// VarExprAST - Expression class for var/in
class VarExprAST : public ExprAST {
    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;
    std::unique_ptr<ExprAST> Body;

public:
    VarExprAST(Lexer::SourceLocation Loc, std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames,
            std::unique_ptr<ExprAST> Body)
        : ExprAST(Loc), VarNames(std::move(VarNames)), Body(std::move(Body)) {}

    llvm::Value *codegen();
};

// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;

public:
    BinaryExprAST(Lexer::SourceLocation Loc,
            char op,
            std::unique_ptr<ExprAST> LHS,
            std::unique_ptr<ExprAST> RHS) :
         ExprAST(Loc), Op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
    llvm::Value *codegen() override;
};

// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST> > Args;

public:
    CallExprAST(Lexer::SourceLocation Loc, const std::string &Callee,
            std::vector<std::unique_ptr<ExprAST> > Args) :
        ExprAST(Loc), Callee(Callee), Args(std::move(Args)) {}
    llvm::Value *codegen() override;
};

// PrototypeAST - This class represents the "prototype" for a function
// which captures its name, and its argument names (this implicitly the number
// of arguments the function takes).
// Also supports user-defined operators.
class PrototypeAST {
    std::string Name;
    std::vector<std::string> Args;
    bool IsOperator;
    unsigned Precedence; // Precedence if a binary op.
    int Line;

public:
    PrototypeAST(Lexer::SourceLocation Loc, const std::string &name,
            std::vector<std::string> Args, bool IsOperator = false, unsigned Prec = 0)
        : Name(name), Args(std::move(Args)), IsOperator(IsOperator),
        Precedence(Prec), Line(Loc.Line) {};
    llvm::Function *codegen();
    const std::string &getName() const { return Name; }

    bool isUnaryOp() const { return IsOperator && Args.size() == 1; }
    bool isBinaryOp() const { return IsOperator && Args.size() == 2; }

    char getOperatorName() const {
        assert(isUnaryOp() || isBinaryOp());
        return Name[Name.size()-1];
    }

    unsigned getBinaryPrecedence() const { return Precedence; }
    int getLine() const { return Line; }
};

// FunctionAST - This class represents a function definition itself.
class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<std::vector<std::unique_ptr<ExprAST>>> Body;

public:
    FunctionAST(std::unique_ptr<PrototypeAST> Proto,
                std::unique_ptr<std::vector<std::unique_ptr<ExprAST>>> Body) :
    Proto(std::move(Proto)), Body(std::move(Body)) {}
    llvm::Function *codegen();
};

// IfExprAST - Expression class for if/then/else
class IfExprAST : public ExprAST {
    std::unique_ptr<ExprAST> Cond, Then, Else;

public:
    IfExprAST(Lexer::SourceLocation Loc, std::unique_ptr<ExprAST> Cond, std::unique_ptr<ExprAST> Then,
            std::unique_ptr<ExprAST> Else)
        : ExprAST(Loc), Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}
    llvm::Value *codegen();
};

// ForExprAST - Expression class for for/in.
class ForExprAST : public ExprAST {
    std::string VarName;
    std::unique_ptr<ExprAST> Start, End, Step, Body;

public:
    ForExprAST(Lexer::SourceLocation Loc, const std::string &VarName, std::unique_ptr<ExprAST> Start,
            std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step,
            std::unique_ptr<ExprAST> Body)
        : ExprAST(Loc), VarName(VarName), Start(std::move(Start)), End(std::move(End)),
        Step(std::move(Step)), Body(std::move(Body)) {}
    llvm::Value *codegen();
};

// UnaryExprAST - Expression class for a unary operator.
class UnaryExprAST : public ExprAST {
    char Opcode;
    std::unique_ptr<ExprAST> Operand;

public:
    UnaryExprAST(Lexer::SourceLocation Loc, char Opcode, std::unique_ptr<ExprAST> Operand)
        : ExprAST(Loc), Opcode(Opcode), Operand(std::move(Operand)) {}
    llvm::Value *codegen();
};

#endif
