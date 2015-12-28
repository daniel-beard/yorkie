## yorkie [![Build Status](https://travis-ci.org/daniel-beard/yorkie.svg)](https://travis-ci.org/daniel-beard/yorkie)
- yorkie is a toy ~~dog~~ language, implemented as a learning experience.
- It is in *no* way ready to use for anything substantial.
- Based on tutorial found here: http://llvm.org/docs/tutorial/LangImpl1.html

### License
- MIT

### Progress
- [X] Chapter 1: Introduction and the Lexer
- [X] Chapter 2: Parser and AST
- [X] Chapter 3: Code generation to LLVM IR 
- [X] Chapter 4: Adding JIT and optimizer support 
- [X] Chapter 5: Extending the language: control flow 
- [ ] Chapter 6: Extending the language: user-defined operators

### Language Syntax
```
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
- Run the command `make build`
- Run the binary `./toy`

### References / TODO
- http://llvm.org/docs/LangRef.html contains references to other interesting instructions that should be relatively easy to add to this language.
- http://llvm.org/docs/LangRef.html#call-instruction LLVM call instructions
- https://en.wikipedia.org/wiki/Visitor_pattern Visitor pattern, would be better to use for codegen.
- http://llvm.org/docs/WritingAnLLVMPass.html#what-passmanager-does - LLVM Passes (used for optimizations)
- Analyze if/then/else statements using the `opt` tool - http://llvm.org/docs/tutorial/LangImpl5.html#llvm-ir-for-if-then-else
