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
#include <string>

class ASTContext;
typedef std::function<ASTContext(void)> PassClosure;

namespace Yorkie {

class Pass {
public:
    Pass(std::string Name, PassClosure Function) :
        Name(Name), Function(Function) {}
    void run(ASTContext context);

private:
    std::string Name;
    PassClosure Function;
};

class Driver {

public:
    Driver() { }
    void run(ASTContext context);
    void add(Pass pass);

private:
    std::vector<Pass> Passes = {};
};
}

#endif /* Driver_h */
