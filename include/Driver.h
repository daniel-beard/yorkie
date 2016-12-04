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
typedef std::function<void(void)> PassClosure;

namespace Yorkie {

class Pass {
public:
    Pass(std::string Name, PassClosure Function) :
        Name(Name), Function(Function) {}
    void run();

private:
    std::string Name;
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
