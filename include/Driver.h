//
//  Driver.h
//  yorkie
//
//  Created by Daniel Beard on 9/3/16.
//
//

#ifndef Driver_h
#define Driver_h 

#include <vector>
#include <functional>
#include <utility>
#include "ASTContext.h"

typedef std::function<ASTContext(void)> PassClosure;

namespace Yorkie {

class Pass {
public:
    Pass(std::string Name, ASTContext Context, PassClosure Function) :
        Name(Name), Context(Context), Function(Function) {}

    //TODO: Might need to take into account ASTContext here...
    void run();

private:
    std::string Name;
    ASTContext Context;
    PassClosure Function;
};

class Driver {

public:
    Driver() { }
    void run();
    void add(Pass pass);

private:
    std::vector<Pass> Passes = {};

};

}

#endif /* Driver_h */