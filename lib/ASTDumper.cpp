//
//  ASTDumper.cpp
//  yorkie
//
//  Created by Daniel Beard on 11/26/16.
//
//

#include <stdio.h>
#include "AST.h"
#include "ASTContext.h"
#include "ASTVisitor.h"
#include "ASTDumper.h"
#include <llvm/Support/Casting.h>

using namespace llvm;

raw_ostream &indent(raw_ostream &O, int size) {
    return O << std::string(size, ' ');
}

std::string lineInfo(ExprAST *expr) {
    std::string buf;
    llvm::raw_string_ostream OS(buf);
    OS << "<line:" << expr->getLine() << ":" << expr->getCol() << ">";
    return OS.str();
}

void ASTDumper::run(ASTContext &context) {
    for (auto function : context.Functions) {
        visitFunction(*function);
    }
}

void ASTDumper::visitExpr(ExprAST *expr) {
    if (isa<IfExprAST>(expr))
        visitIfExpr(dyn_cast<IfExprAST>(expr));
    else if (isa<CompoundExprAST>(expr))
        visitCompoundExpr(dyn_cast<CompoundExprAST>(expr));
    else if (isa<NumberExprAST>(expr))
        visitNumberExpr(dyn_cast<NumberExprAST>(expr));
    else if (isa<VariableExprAST>(expr))
        visitVariableExpr(dyn_cast<VariableExprAST>(expr));
    else if (isa<VarExprAST>(expr))
        visitVarExpr(dyn_cast<VarExprAST>(expr));
    else if (isa<BinaryExprAST>(expr))
        visitBinaryExpr(dyn_cast<BinaryExprAST>(expr));
    else if (isa<CallExprAST>(expr))
        visitCallExpr(dyn_cast<CallExprAST>(expr));
    else if (isa<IfExprAST>(expr))
        visitIfExpr(dyn_cast<IfExprAST>(expr));
    else if (isa<ForExprAST>(expr))
        visitForExpr(dyn_cast<ForExprAST>(expr));
    else if (isa<UnaryExprAST>(expr))
        visitUnaryExpr(dyn_cast<UnaryExprAST>(expr));
}

void ASTDumper::visitCompoundExpr(CompoundExprAST *expr) {

}

void ASTDumper::visitNumberExpr(NumberExprAST *expr) {
    indent(Stream, Indent) << "NumberExprAST " << lineInfo(expr);
    Stream << "'" << expr->getVal() << "'\n";
}

void ASTDumper::visitVariableExpr(VariableExprAST *expr) {
    indent(Stream, Indent) << "VariableExprAST " << lineInfo(expr) << " '" << expr->getName() << "'\n";
}

void ASTDumper::visitVarExpr(VarExprAST *expr) {

}

void ASTDumper::visitBinaryExpr(BinaryExprAST *expr) {
    indent(Stream, Indent) << "BinaryExprAST " << lineInfo(expr) << " '" << expr->getOperator() << "'\n";
    Indent++;
    visitExpr(expr->getLHS());
    visitExpr(expr->getRHS());
    Indent--;
}

void ASTDumper::visitCallExpr(CallExprAST *expr) {

}

void ASTDumper::visitPrototype(PrototypeAST *prototype) {
    indent(Stream, Indent) << "PrototypeAST <line:" << prototype->getLine() << ":0> ";
    Stream << prototype->getName() << " ";
    // Args
    Stream << "'";
    for (std::string arg : prototype->getArgs()) {
        Stream << arg << " ";
    }
    Stream << "' ";
    Stream << "Op? " << (prototype->isBinaryOp() || prototype->isUnaryOp()) << "\n";
}

void ASTDumper::visitFunction(FunctionAST function) {
    indent(Stream, Indent) << "FunctionAST\n";
    Indent += 1;
    for (auto expr : *function.Body) {
        visitPrototype(function.Proto.get());
        visitExpr(expr.get());
    }
}

void ASTDumper::visitIfExpr(IfExprAST *expr) {
    indent(Stream, Indent) << "IfExprAST " << lineInfo(expr) << "\n";
    Indent++;
    visitExpr(expr->getCond());
    visitExpr(expr->getThen());
    visitExpr(expr->getElse());
    Indent--;
}

void ASTDumper::visitForExpr(ForExprAST *expr) {
    
}

void ASTDumper::visitUnaryExpr(UnaryExprAST *expr) {
    
}
