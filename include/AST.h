
#ifndef YORKIE_AST_H
#define YORKIE_AST_H

#include <iostream>
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Casting.h"
#include "Lexer.h"

//===============================================
// AST.h
//
//===============================================

// ExprAST - Base class for all expression nodes.
class ExprAST {

public:

    /// Discriminator for LLVM-style RTTI (dyn_cast<> et al.)
    /// http://llvm.org/docs/HowToSetUpLLVMStyleRTTI.html#basic-setup
    enum ExprKind {
        EK_CompoundExpr,
        EK_NumberExpr,
        EK_VariableExpr,
        EK_VarExpr,
        EK_BinaryExpr,
        EK_CallExpr,
        EK_Prototype,
        EK_Function,
        EK_IfExpr,
        EK_ForExpr,
        EK_UnaryExpr,
    };

private:
    Lexer::SourceLocation Loc;
    const ExprKind Kind;

public:
    ExprAST(Lexer::SourceLocation Loc, ExprKind Kind) : Loc(Loc), Kind(Kind) {}
    virtual ~ExprAST() {}
    virtual llvm::Value *codegen() = 0;
    int getLine() const { return Loc.Line; }
    int getCol() const { return Loc.Col; }
    ExprKind getKind() const { return Kind; }
};

// CompoundExprAST - Multiple expressions, used within FunctionAST or IfExprAST
class CompoundExprAST: public ExprAST {
    std::shared_ptr<std::vector<std::shared_ptr<ExprAST>>> Body;

public:
    CompoundExprAST(Lexer::SourceLocation Loc, std::shared_ptr<std::vector<std::shared_ptr<ExprAST>>> Body) : ExprAST(Loc, EK_CompoundExpr), Body(std::move(Body)) {};
    static bool classof(const ExprAST *E) { return E->getKind() == EK_CompoundExpr; }
};

// NumberExprAST - Expression class for numeric literals like "1.0"
class NumberExprAST: public ExprAST {
    double Val;

public:
    NumberExprAST(Lexer::SourceLocation Loc, double Val) : ExprAST(Loc, EK_NumberExpr), Val(Val) {}
    llvm::Value *codegen() override;
    static bool classof(const ExprAST *E) { return E->getKind() == EK_NumberExpr; }
    double getVal() { return Val; }
};

// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
    std::string Name;

public:
    VariableExprAST(Lexer::SourceLocation Loc, const std::string &Name) : ExprAST(Loc, EK_VariableExpr), Name(Name) {};
    const std::string &getName() const { return Name; }
    llvm::Value *codegen() override;
    static bool classof(const ExprAST *E) { return E->getKind() == EK_VariableExpr; }

    std::string getName() { return Name; }
};

// VarExprAST - Expression class for var/in
class VarExprAST : public ExprAST {
    std::vector<std::pair<std::string, std::shared_ptr<ExprAST>>> VarNames;
    std::shared_ptr<ExprAST> Body;

public:
    VarExprAST(Lexer::SourceLocation Loc, std::vector<std::pair<std::string, std::shared_ptr<ExprAST>>> VarNames,
            std::shared_ptr<ExprAST> Body)
        : ExprAST(Loc, EK_VarExpr), VarNames(std::move(VarNames)), Body(std::move(Body)) {}

    llvm::Value *codegen();
    static bool classof(const ExprAST *E) { return E->getKind() == EK_VarExpr; }
};

// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
    char Op;
    std::shared_ptr<ExprAST> LHS, RHS;

public:
    BinaryExprAST(Lexer::SourceLocation Loc,
            char op,
            std::shared_ptr<ExprAST> LHS,
            std::shared_ptr<ExprAST> RHS) :
         ExprAST(Loc, EK_BinaryExpr), Op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
    llvm::Value *codegen() override;
    static bool classof(const ExprAST *E) { return E->getKind() == EK_BinaryExpr; }
    ExprAST* getLHS() { return LHS.get(); }
    ExprAST* getRHS() { return RHS.get(); }
    char getOperator() { return Op; }
};

// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::shared_ptr<ExprAST> > Args;

public:
    CallExprAST(Lexer::SourceLocation Loc, const std::string &Callee,
            std::vector<std::shared_ptr<ExprAST> > Args) :
        ExprAST(Loc, EK_CallExpr), Callee(Callee), Args(std::move(Args)) {}
    llvm::Value *codegen() override;
    static bool classof(const ExprAST *E) { return E->getKind() == EK_CallExpr; }
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
    std::vector<std::string> getArgs() { return Args; }
};

// FunctionAST - This class represents a function definition itself.
class FunctionAST {

//private:
//TODO: These should be private.
public:
    std::shared_ptr<PrototypeAST> Proto;
    std::shared_ptr<std::vector<std::shared_ptr<ExprAST>>> Body;

public:

    FunctionAST(std::shared_ptr<PrototypeAST> Proto,
                std::shared_ptr<std::vector<std::shared_ptr<ExprAST>>> Body) :
    Proto(std::move(Proto)), Body(std::move(Body)) {}
    llvm::Function *codegen();

    PrototypeAST* getPrototype() { return Proto.get(); }

};

// IfExprAST - Expression class for if/then/else
class IfExprAST : public ExprAST {
    std::shared_ptr<ExprAST> Cond, Then, Else;

public:
    IfExprAST(Lexer::SourceLocation Loc, std::shared_ptr<ExprAST> Cond, std::shared_ptr<ExprAST> Then,
            std::shared_ptr<ExprAST> Else)
        : ExprAST(Loc, EK_IfExpr), Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}
    llvm::Value *codegen();
    static bool classof(const ExprAST *E) { return E->getKind() == EK_IfExpr; }

    ExprAST* getCond() { return Cond.get(); }
    ExprAST* getThen() { return Then.get(); }
    ExprAST* getElse() { return Else.get(); }
};

// ForExprAST - Expression class for for/in.
class ForExprAST : public ExprAST {
    std::string VarName;
    std::shared_ptr<ExprAST> Start, End, Step, Body;

public:
    ForExprAST(Lexer::SourceLocation Loc, const std::string &VarName, std::shared_ptr<ExprAST> Start,
            std::shared_ptr<ExprAST> End, std::shared_ptr<ExprAST> Step,
            std::shared_ptr<ExprAST> Body)
        : ExprAST(Loc, EK_ForExpr), VarName(VarName), Start(std::move(Start)), End(std::move(End)),
        Step(std::move(Step)), Body(std::move(Body)) {}
    llvm::Value *codegen();
    static bool classof(const ExprAST *E) { return E->getKind() == EK_ForExpr; }
};

// UnaryExprAST - Expression class for a unary operator.
class UnaryExprAST : public ExprAST {
    char Opcode;
    std::shared_ptr<ExprAST> Operand;

public:
    UnaryExprAST(Lexer::SourceLocation Loc, char Opcode, std::shared_ptr<ExprAST> Operand)
        : ExprAST(Loc, EK_UnaryExpr), Opcode(Opcode), Operand(std::move(Operand)) {}
    llvm::Value *codegen();
    static bool classof(const ExprAST *E) { return E->getKind() == EK_UnaryExpr; }
};

#endif
