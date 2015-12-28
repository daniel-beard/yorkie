## Kaleidoscope
Toy language created by following: http://llvm.org/docs/tutorial/LangImpl1.html

### License
- MIT

### Progress
- [X] Chapter 1: Introduction and the Lexer
- [X] Chapter 2: Parser and AST
- [X] Chapter 3: Code generation to LLVM IR 
- [X] Chapter 4: Adding JIT and optimizer support 
- [ ] Chapter 5: Extending the language: control flow - http://llvm.org/docs/tutorial/LangImpl5.html

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
