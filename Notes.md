## Notes

### Mutable Variables
- Must be in SSA form
- We are going to use the `mem2reg` LLVM IR optimizer, instead of inserting PHI nodes directly.
- `mem2reg` promotes `alloca`s into registers, inserting PHI nodes where necessary.
- Looking at `alloca` code with the `opt` tool and `mem2reg`, we can see how it transforms the code: `llvm-as < example.ll | opt -mem2reg | llvm-dis`

1. Each mutable variable becomes a stack allocation.
2. Each read of the variable becomes a load from the stack.
3. Each update of the variable becomes a store to the stack.
4. Taking the address of a variable just uses the stack address directly.

#### mem2reg caveats
- mem2reg is alloca-driven: it looks for allocas and if it can handle them, it promotes them. It does not apply to global variables or heap allocations.
- mem2reg only looks for alloca instructions in the entry block of the function. Being in the entry block guarantees that the alloca is only executed once, which makes analysis simpler.
- mem2reg only promotes allocas whose uses are direct loads and stores. If the address of the stack object is passed to a function, or if any funny pointer arithmetic is involved, the alloca will not be promoted.
- mem2reg only works on allocas of first class values (such as pointers, scalars and vectors), and only if the array size of the allocation is 1 (or missing in the .ll file). mem2reg is not capable of promoting structs or arrays to registers. Note that the “scalarrepl” pass is more powerful and can promote structs, “unions”, and arrays in many cases.

#### Additional language features required:
- The ability to mutate variables with the ‘=’ operator.
- The ability to define new variables.

### Compiling C down to LLVM IR
`clang -S -emit-llvm foo.c`

### Debugging with source information
- Compile examples/fib.yk down to a.out
- Make sure fib.yk exists in the working directory
- `lldb a.out`
- `(lldb) breakpoint set --name foo`
- `(lldb) run`

```
(lldb) run
Process 21213 launched: '/Users/dbeard/Dev/yorkie/a.out' (x86_64)
Process 21213 stopped
* thread #1: tid = 0xb2954, 0x0000000100000dfc a.out`foo + 12 at fib.yk:3, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1
    frame #0: 0x0000000100000dfc a.out`foo + 12 at fib.yk:3
   1   	extern putchard(x);
   2   	def foo()
-> 3   	    putchard(72) # H
   4   	    putchard(69) # E
   5   	    putchard(76) # L
   6   	    putchard(76) # L
   7   	    putchard(79) # O
```

### Other compiling notes
- Compile several files down to .ll format

```
clang -S -emit-llvm foo.c -o foo.ll
clang -S -emit-llvm bar.c -o bar.ll
```

Compile down to bitcode:

```
llvm-as foo.ll -o foo.bc
llvm-as bar.ll -o bar.bc
```

Link bitcode files together:

```
llvm-link foo.bc bar.bc -o output.bc
```

Run bitcode files:

```
lli output.bc
```

Statically compile bitcode files:

```
llc output.bc 
```

llc can even generate cpp code from bitcode:

```
llc -march=cpp output.bc
```
