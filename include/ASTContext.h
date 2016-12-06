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

typedef std::vector<std::shared_ptr<FunctionAST>> FunctionCollectionType;

class ASTContext {

private:
    std::string FileName;

public:
    // Members
    FunctionCollectionType Functions;

    // Initializer
    explicit ASTContext(std::string FileName) : FileName(FileName) {
        Functions = FunctionCollectionType();
    }

    // Methods
    void addFunction(std::shared_ptr<FunctionAST> function);

};

#endif
