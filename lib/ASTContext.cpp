//
//  ASTContext.cpp
//  yorkie
//
//  Created by Daniel Beard on 9/3/16.
//
//

#include "AST.h"
#include "ASTContext.h"

void ASTContext::addFunction(std::unique_ptr<FunctionAST> function) {
    Functions->push_back(std::move(function));
}
