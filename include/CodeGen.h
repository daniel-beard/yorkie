
#ifndef CodeGen_h
#define CodeGen_h

//===============================================
// CodeGen.h
//
// Converts AST nodes to LLVM IR
//
//===============================================

#include "ASTContext.h"

class CodeGen {

public:
    CodeGen();
    void run(ASTContext context);

private:

};


#endif /* CodeGen_h */
