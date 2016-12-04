//
//  ASTContext.h
//  yorkie
//
//  Created by Daniel Beard on 9/3/16.
//
//

#ifndef YORKIE_ASTCONTEXT_H
#define YORKIE_ASTCONTEXT_H

#include <stdio.h>
#include <iostream>
#include <vector>
#include <functional>
#include "AST.h"

typedef std::vector<std::unique_ptr<FunctionAST>> FunctionCollectionType;

class ASTContext {

private:
    std::string FileName;

public:
    // Members
    std::unique_ptr<FunctionCollectionType> Functions;

    // Initializer
    explicit ASTContext(std::string FileName) : FileName(FileName) {
        Functions = llvm::make_unique<FunctionCollectionType>(FunctionCollectionType());
    }

    // Methods
    void addFunction(std::unique_ptr<FunctionAST> function);

};

#endif
