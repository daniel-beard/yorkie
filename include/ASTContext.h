//
//  ASTContext.h
//  yorkie
//
//  Created by Daniel Beard on 9/3/16.
//
//

#ifndef ASTContext_hpp
#define ASTContext_hpp

#include <stdio.h>
#include <iostream>
#include "llvm/ADT/ArrayRef.h"
#include "AST.h"
#include <functional>

class ASTContext {

private:
    std::string FileName;
    llvm::ArrayRef<FunctionAST> Functions;

public:

    // Initializer
    ASTContext(std::string FileName) : FileName(FileName) {}

    // Methods
    void addFunction(std::unique_ptr<FunctionAST> function);

};

#endif /* ASTContext_h */
