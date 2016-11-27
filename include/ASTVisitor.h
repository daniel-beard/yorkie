//
//  ASTVisitor.h
//  yorkie
//
//  Created by Daniel Beard on 9/5/16.
//
//

#ifndef YORKIE_ASTVISITOR_H
#define YORKIE_ASTVISITOR_H

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

class ASTVisitor {
public:
    virtual void visitExpr(ExprAST expr) = 0;
    virtual void visitCompoundExpr(CompoundExprAST expr) = 0;
    virtual void visitNumberExpr(NumberExprAST expr) = 0;
    virtual void visitVariableExpr(VariableExprAST expr) = 0;
    virtual void visitVarExpr(VarExprAST expr) = 0;
    virtual void visitBinaryExpr(BinaryExprAST expr) = 0;
    virtual void visitCallExpr(CallExprAST expr) = 0;
    virtual void visitPrototype(PrototypeAST prototype) = 0;
    virtual void visitFunction(FunctionAST function) = 0;
    virtual void visitIfExpr(IfExprAST expr) = 0;
    virtual void visitForExpr(ForExprAST expr) = 0;
    virtual void visitUnaryExpr(UnaryExprAST expr) = 0;

    virtual ~ASTVisitor() = 0;
};

#endif
