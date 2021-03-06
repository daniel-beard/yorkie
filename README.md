## yorkie [![Build Status](https://travis-ci.org/daniel-beard/yorkie.svg)](https://travis-ci.org/daniel-beard/yorkie)
- yorkie is a toy ~~dog~~ language, implemented as a learning experience.
- It is *not* intended for actual use.
- Based on the LLVM tutorial: http://llvm.org/docs/tutorial/LangImpl1.html

### Language Syntax
```
# Top level expressions
4+5;

# Control Flow - if/then/else
if x < 3 then
    1
else
    2
end

# Forward declarations and calling 'C' code
extern sin(x) # Because we link against libm, we can use that function natively here!
sin(10)

# Control Flow - for loops
# (initial value), (end condition), (optional step value)
for i = 1, i < n, 1.0 in
    putchard(i)
end

# User defined unary operators
def unary-(v)
    0-v
end

# User defined binary operators
def binary> 10 (LHS RHS)
    RHS < LHS
end

# Functions
# Compute the x'th fibonacci number.
def fib(x)
    if x < 3 then
        1
    else
        fib(x-1)+fib(x-2)
    end
end

# This expression will compute the 40th number.
fib(40)

# Mutable variables
def foo()
    var x = 10 in
        x = 11
    end
end
```

### Building
- Make sure you have LLVM setup - http://llvm.org/docs/GettingStarted.html
- Run the command `cmake .`
- Run `make`
- Build one of the examples: `./yorkie < examples/fib.yk 2>&1 | clang -x ir -`
- Run the example: `./a.out`

### Testing
- `cmake .`
- `cmake --build .`
- `ctest -VV`

### License
- MIT

### References / Links
- http://llvm.org/docs/LangRef.html contains references to other interesting instructions that should be relatively easy to add to this language.
- http://llvm.org/docs/LangRef.html#call-instruction LLVM call instructions
- https://en.wikipedia.org/wiki/Visitor_pattern Visitor pattern, would be better to use for codegen.
- http://llvm.org/docs/WritingAnLLVMPass.html#what-passmanager-does - LLVM Passes (used for optimizations)
