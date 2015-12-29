## yorkie [![Build Status](https://travis-ci.org/daniel-beard/yorkie.svg)](https://travis-ci.org/daniel-beard/yorkie)
- yorkie is a toy ~~dog~~ language, implemented as a learning experience.
- It is in *not* intended for actual use.
- Based on the LLVM tutorial: http://llvm.org/docs/tutorial/LangImpl1.html

### License
- MIT

### Progress
- [X] Chapter 1: Introduction and the Lexer
- [X] Chapter 2: Parser and AST
- [X] Chapter 3: Code generation to LLVM IR 
- [X] Chapter 4: Adding JIT and optimizer support 
- [X] Chapter 5: Extending the language: control flow 
- [X] Chapter 6: Extending the language: user-defined operators
- [ ] Chapter 7: Extending the language: Mutable Variables.

### Language Syntax
```
# Top level expressions
4+5;

# Control Flow - if/then/else
if x < 3 then
    1
else 
    2

# Control Flow - for loops
# (initial value), (end condition), (optional step value)
for i = 1, i < n, 1.0 in 
    putchard(i)

# User defined unary operators
def unary-(v)
    0-v

# User defined binary operators
def binary> 10 (LHS RHS)
    RHS < LHS

# Functions
# Compute the x'th fibonacci number.
def fib(x)
    if x < 3 then
        1
    else
        fib(x-1)+fib(x-2)

# This expression will compute the 40th number.
fib(40)
```

### Building
- Make sure you have LLVM setup - http://llvm.org/docs/GettingStarted.html
- Run the command `make`
- Run the binary `./toy`

### References / TODO
- http://llvm.org/docs/LangRef.html contains references to other interesting instructions that should be relatively easy to add to this language.
- http://llvm.org/docs/LangRef.html#call-instruction LLVM call instructions
- https://en.wikipedia.org/wiki/Visitor_pattern Visitor pattern, would be better to use for codegen.
- http://llvm.org/docs/WritingAnLLVMPass.html#what-passmanager-does - LLVM Passes (used for optimizations)
