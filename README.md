## Kaleidoscope
Toy language created by following: http://llvm.org/docs/tutorial/LangImpl1.html

### License
- MIT

### Progress
- [X] Chapter 1: Introduction and the Lexer
- [X] Chapter 2: Parser and AST
- [ ] Chapter 3: Code generation to LLVM IR - http://llvm.org/docs/tutorial/LangImpl3.html

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

### References / TODO
- http://llvm.org/docs/LangRef.html contains references to other interesting instructions that should be relatively easy to add to this language.
- http://llvm.org/docs/LangRef.html#call-instruction LLVM call instructions
- https://en.wikipedia.org/wiki/Visitor_pattern Visitor pattern, would be better to use for codegen.
