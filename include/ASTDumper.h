//
//  ASTDumper.h
//  yorkie
//
//  Created by Daniel Beard on 9/7/16.
//
//

#ifndef YORKIE_ASTDUMPER_H
#define YORKIE_ASTDUMPER_H

#include "ASTVisitor.h"

// Forward declarations
class ExprAST;
class CompoundExprAST;
class NumberExprAST;
class VariableExprAST;
class VarExprAST;
class BinaryExprAST;
class CallExprAST;
class PrototypeAST;
class FunctionAST;
class IfExprAST;
class ForExprAST;
class UnaryExprAST;

class ASTDumper: public virtual ASTVisitor {

private:
    void visitExpr(ExprAST expr);
    void visitCompoundExpr(CompoundExprAST expr);
    void visitNumberExpr(NumberExprAST expr);
    void visitVariableExpr(VariableExprAST expr);
    void visitVarExpr(VarExprAST expr);
    void visitBinaryExpr(BinaryExprAST expr);
    void visitCallExpr(CallExprAST expr);
    void visitPrototype(PrototypeAST prototype);
    void visitFunction(FunctionAST function);
    void visitIfExpr(IfExprAST expr);
    void visitForExpr(ForExprAST expr);
    void visitUnaryExpr(UnaryExprAST expr);

public:
    ASTDumper() {};
    ~ASTDumper() {};
    void run(ASTContext context);
};

#endif /* ASTDumper_h */
