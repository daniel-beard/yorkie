
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/CommandLine.h"
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <iostream>
#include <memory>
#include "../include/KaleidoscopeJIT.h"
#include "Lexer.h"
#include "AST.h"
#include "Parser.h"

using namespace llvm;
using namespace llvm::orc;

// Lexer
static Lexer::Lexer lexer;

// IR Builder.
static IRBuilder<> Builder(getGlobalContext());

struct DebugInfo {
    DICompileUnit *TheCU;
    DIType *DblTy;
    std::vector<DIScope *> LexicalBlocks;

    void emitLocation(ExprAST *AST);
    DIType *getDoubleTy();
} KSDbgInfo;

// =============================================================================
// Error* - These are little helper functions for error handling.
// =============================================================================

std::unique_ptr<ExprAST> Error(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}
std::unique_ptr<PrototypeAST> ErrorP(const char *Str) {
    Error(Str);
    return nullptr;
}
std::unique_ptr<FunctionAST> ErrorF(const char *Str) {
    Error(Str);
    return nullptr;
}

// ================================================================
// Debug Info Support
// ================================================================

// FnScopeMap keeps a map of each function to the scope that it represents.
// A DISubpgrogram is also a DIScope.
std::vector<DIScope *> LexicalBlocks;
std::map<const PrototypeAST *, DIScope *> FnScopeMap;
static std::unique_ptr<DIBuilder> DBuilder;

DIType *DebugInfo::getDoubleTy() {
    if (DblTy)
        return DblTy;

    DblTy = DBuilder->createBasicType("double", 64, 64, dwarf::DW_ATE_float);
    return DblTy;
}

// Tells main IRBuilder where we are, but also what scope we are in.
// Scope is a stack, can either be in the main file scope, or in the function scope etc.
void DebugInfo::emitLocation(ExprAST *AST) {
    if (!AST)
        return Builder.SetCurrentDebugLocation(DebugLoc());
    DIScope *Scope;
    if (LexicalBlocks.empty())
        Scope = TheCU;
    else
        Scope = LexicalBlocks.back();
    Builder.SetCurrentDebugLocation(
            DebugLoc::get(AST->getLine(), AST->getCol(), Scope));
}

static DISubroutineType *CreateFunctionType(unsigned NumArgs, DIFile *Unit) {
    SmallVector<Metadata *, 8> EltTys;
    DIType *DblTy = KSDbgInfo.getDoubleTy();

    // Add the result type.
    EltTys.push_back(DblTy);

    for (unsigned i = 0, e = NumArgs; i != e; ++i)
        EltTys.push_back(DblTy);

    return DBuilder->createSubroutineType(DBuilder->getOrCreateTypeArray(EltTys));
}

// ================================================================
// Code Generation
// ================================================================

// Used during code generation
// TheModule is an LLVM construct that contains functions and global variables.
// It owns the memory for all of the IR we generate. (Also why codegen() returns raw Value* rather than unique_ptr(Value)
// Builder is a helper object that makes it easy to generate LLVM instructions.
// NamedValues keeps track of which values are defined in the current scope,
// and what their LLVM representation is.
// NamedValues holds the memory location of each mutable variable.
static std::unique_ptr<Module> TheModule;
static std::map<std::string, AllocaInst*> NamedValues;
static std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
static std::unique_ptr<KaleidoscopeJIT> TheJIT;
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

Value *ErrorV(const char *Str) {
    Error(Str);
    return nullptr;
}

// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of the function.
// This is used for mutable variables etc.
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction, const std::string &VarName) {
    IRBuilder<> TmpB(&TheFunction->getEntryBlock(), TheFunction->getEntryBlock().begin());
    return TmpB.CreateAlloca(Type::getDoubleTy(getGlobalContext()), 0, VarName.c_str());
}

Function *getFunction(std::string Name) {
    // First, see if the function has already been added to the current module.
    if (auto *F = TheModule->getFunction(Name))
        return F;

    // If not, check whether we can codegen the declaration from some existing prototype.
    auto FI = FunctionProtos.find(Name);
    if (FI != FunctionProtos.end())
        return FI->second->codegen();

    // If no existing prototype exists, return null.
    return nullptr;
}

// Generate code for numeric literals
// `APFloat` has the capability of holder fp constants of arbitrary precision.
Value *NumberExprAST::codegen() {
    KSDbgInfo.emitLocation(this);
    return ConstantFP::get(getGlobalContext(), APFloat(Val));
}

// Generate code for variable expressions
Value *VariableExprAST::codegen() {
    // Look this variable up in the function
    Value *V = NamedValues[Name];
    if (!V)
        ErrorV("Unknown variable name");

    // Emit debug location
    KSDbgInfo.emitLocation(this);

    // Load the value
    return Builder.CreateLoad(V, Name.c_str());
}

// Generate code for binary expressions
// Recursively emit code for the LHS then the RHS then compute the result.
// LLVM instructions have strict rules, e.g. add - LHS and RHS must have the same type.
// fcmp always returns an 'i1' value (a one bit integer).
// We want 0.0 or 1.0 for this, so we use a uitofp instruction.
Value *BinaryExprAST::codegen() {
    // Emit debug location
    KSDbgInfo.emitLocation(this);

    // Special case '=' because we don't want to emit the LHS as an expression
    if (Op == '=') {
        // Assignment requires the LHS to be an identifier.
        // TODO:
        // This assumes that we're building without RTTI because LLVM builds that way by
        // default. If you build LLVM with RTTI this can be changed to a dynamic_cast
        // for automatic error checking.
        VariableExprAST *LHSE = static_cast<VariableExprAST*>(LHS.get());
        if (!LHSE)
            return ErrorV("destination of '=' must be a variable");

        // Codegen the RHS
        Value *Val = RHS->codegen();
        if (!Val)
            return nullptr;

        // Look up the name.
        Value *Variable = NamedValues[LHSE->getName()];
        if (!Variable)
            return ErrorV("Unknown variable name");

        Builder.CreateStore(Val, Variable);
        return Val;
    }

    Value *L = LHS->codegen();
    Value *R = RHS->codegen();

    if (!L || !R)
        return nullptr;

    switch (Op) {
    case '+':
        return Builder.CreateFAdd(L, R, "addtmp");
    case '-':
        return Builder.CreateFSub(L, R, "subtmp");
    case '*':
        return Builder.CreateFMul(L, R, "multmp");
    case '<':
        L = Builder.CreateFCmpULT(L, R, "cmptmp");
        // Convert bool 0/1 to double 0.0 of 1.0
        return Builder.CreateUIToFP(L, Type::getDoubleTy(getGlobalContext()), "booltmp");
    default:
        break;
    }

    // If it wasn't a builtin binary operator, it must be a user defined one.
    // Loop up the operator in the symbol table.
    // Emit a call to it.
    Function *F = getFunction(std::string("binary") + Op);
    assert(F && "binary operator not found!");

    // Binary operators are just function calls, so we just emit a function call.
    Value *Ops[2] = { L,R };
    return Builder.CreateCall(F, Ops, "binop");
}

// Generate code for function calls
// Lookup function name in the LLVM Module's symbol table.
// We use the same name in the symbol table as what the user specifies.
// Note that LLVM uses the native C calling conventions by default,
// allowing these calls to also call into standard lib functions like `sin` and `cos`.
Value *CallExprAST::codegen() {
    // Emit debug location
    KSDbgInfo.emitLocation(this);

    // Look up the name in the global module table
    Function *CalleeF = getFunction(Callee);
    if (!CalleeF)
        return ErrorV("Unknown function referenced");

    // If argument mismatch error
    if (CalleeF->arg_size() != Args.size())
        return ErrorV("Incorrect # arguments passed");

    std::vector<Value *> ArgsV;
    for (unsigned i = 0, e = Args.size(); i != e; ++i) {
        ArgsV.push_back(Args[i]->codegen());
        if (!ArgsV.back())
            return nullptr;
    }
    return Builder.CreateCall(CalleeF, ArgsV, "calltmp");
}

// Generate code for function declarations (prototypes)
// All function types are Doubles for now
Function *PrototypeAST::codegen() {

    // Return type. Special case "main" function to return i32 0
    //TODO: Remove once proper type support is added.
    Type *ReturnType = Type::getDoubleTy(getGlobalContext());
    if (Name == "main")
      ReturnType = Type::getInt32Ty(getGlobalContext());

    // Make the function type: double(double, double) etc.
    std::vector<Type*> Doubles(Args.size(),
            Type::getDoubleTy(getGlobalContext()));

    // false specifies this is not a vargs function
    FunctionType *FT = FunctionType::get(ReturnType, Doubles, false);
    // ExternalLinkage means function may be defined outside the current module
    // or that it is callable by functions outside the module.
    Function *F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

    // Set names for all arguments.
    unsigned Idx = 0;
    for (auto &Arg : F->args())
        Arg.setName(Args[Idx++]);

    return F;
}

// Generate code for function bodies.
Function *FunctionAST::codegen() {

    // Transfer ownership of the prototype to the FunctionProtos map, but keep a
    // reference to it for use below.
    auto &P = *Proto;
    FunctionProtos[Proto->getName()] = std::move(Proto);
    Function *TheFunction = getFunction(P.getName());
    if (!TheFunction)
        return nullptr;

    // If this is an operator, install it in the BinopPrecedence map.
    if (P.isBinaryOp())
        Parser::BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

    // Want to make sure that the function doesn't already have a body before we generate one.
    if (!TheFunction->empty())
        return (Function*)ErrorV("Function cannot be redefined.");

    // Create a new basic block to start insertion into.
    BasicBlock *BB = BasicBlock::Create(getGlobalContext(), "entry", TheFunction);
    Builder.SetInsertPoint(BB);

    // Create a subprogram DIE for this function.
    DIFile *Unit = DBuilder->createFile(KSDbgInfo.TheCU->getFilename(),
            KSDbgInfo.TheCU->getDirectory());

    DIScope *FContext = Unit;
    unsigned LineNo = P.getLine();
    unsigned ScopeLine = LineNo;
    // DISubprogram contains a reference to all of our metadata for the function.
    DISubprogram *SP = DBuilder->createFunction(
            FContext, P.getName(), StringRef(), Unit, LineNo,
            CreateFunctionType(TheFunction->arg_size(), Unit), false /* internal linkage */,
            true /* definition */, ScopeLine, DINode::FlagPrototyped, false);
    TheFunction->setSubprogram(SP);

    // Push the current scope
    KSDbgInfo.LexicalBlocks.push_back(SP);

    // Unset the location for the prologue emission (leading instructinos with no
    // location in a function are considered part of the prologue and the debugger
    // will run past them when breaking on a function.
    KSDbgInfo.emitLocation(nullptr);

    // Record the function arguments in the NamedValues map.
    // Add the function arguments to the NamedValues map, so they are accessible to the
    // `VariableExprAST` nodes
    NamedValues.clear();
    unsigned ArgIdx = 0;
    for (auto &Arg : TheFunction->args()) {
        // Create an alloca for this variable.
        AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());

        // Create a debug descriptor for the variable.
        DILocalVariable *D = DBuilder->createParameterVariable(
                SP, Arg.getName(), ++ArgIdx, Unit, LineNo, KSDbgInfo.getDoubleTy(), true);

        DBuilder->insertDeclare(Alloca, D, DBuilder->createExpression(),
                DebugLoc::get(LineNo, 0, SP),
                Builder.GetInsertBlock());

        // Store the initial value into the alloca.
        Builder.CreateStore(&Arg, Alloca);

        // Add arguments to variable symbol table
        NamedValues[Arg.getName()] = Alloca;
    }

    KSDbgInfo.emitLocation(Body.get());

    // If no error, emit the ret instruction, which completes the function.
    if (Value *RetVal = Body->codegen()) {

        // Special case "main"
        // TODO: Remove once proper type support is added
        if (P.getName() == "main") {
          RetVal = ConstantInt::get(getGlobalContext(), APInt(32,0));
        }

        // Finish off the function.
        Builder.CreateRet(RetVal);

        // Pop off the lexical block for the function.
        KSDbgInfo.LexicalBlocks.pop_back();

        // Validate the generated code, checking for consistency. Function is provided by LLVM.
        verifyFunction(*TheFunction);

        return TheFunction;
    }

    // Error reading body, remove function.
    TheFunction->eraseFromParent();

    if (P.isBinaryOp())
        Parser::BinopPrecedence.erase(Proto->getOperatorName());

    // Pop off the lexical block for the function since we added it unconditionally
    KSDbgInfo.LexicalBlocks.pop_back();

    return nullptr;
}

// Generate code for if/then/else expressions.
// We get the condition, and convert to a boolean value, then get the function we are
// currently in, by getting the current blocks parent.
// TheFunction is passed into the `ThenBB` block so it's automatically inserted into the function.
// Then we create the conditional branch that chooses between the blocks.
Value *IfExprAST::codegen() {
    // Emit debug location
    KSDbgInfo.emitLocation(this);

    Value *CondV = Cond->codegen();
    if (!CondV)
        return nullptr;

    // Convert condition to a bool by comparing equal to 0.0
    CondV = Builder.CreateFCmpONE(
            CondV, ConstantFP::get(getGlobalContext(), APFloat(0.0)), "ifcond");

    Function *TheFunction = Builder.GetInsertBlock()->getParent();

    // Create blocks for the then and else cases. Insert the 'then' block at the
    // end of the function
    BasicBlock *ThenBB = BasicBlock::Create(getGlobalContext(), "then", TheFunction);
    BasicBlock *ElseBB = BasicBlock::Create(getGlobalContext(), "else");
    BasicBlock *MergeBB = BasicBlock::Create(getGlobalContext(), "ifcont");

    // Conditional branch
    Builder.CreateCondBr(CondV, ThenBB, ElseBB);

    // Emit then value.
    Builder.SetInsertPoint(ThenBB);

    Value *ThenV = Then->codegen();
    if (!ThenV)
        return nullptr;

    Builder.CreateBr(MergeBB);
    // Codegen of 'Then' can change the current block, update ThenBB for the PHI
    // E.g. Then expression may contain a nested if/then/else, which would change
    // the notion of the current block, so we have to get an up-to-date value for code that
    // will set up the Phi node.
    ThenBB = Builder.GetInsertBlock();

    // Emit else block.
    TheFunction->getBasicBlockList().push_back(ElseBB);
    Builder.SetInsertPoint(ElseBB);

    Value *ElseV = Else->codegen();
    if (!ElseV)
        return nullptr;

    Builder.CreateBr(MergeBB);
    // codegen of 'Else' can change the current block, update ElseBB for the PHI.
    ElseBB = Builder.GetInsertBlock();

    // Emit merge block.
    TheFunction->getBasicBlockList().push_back(MergeBB);
    Builder.SetInsertPoint(MergeBB);
    PHINode *PN = Builder.CreatePHI(Type::getDoubleTy(getGlobalContext()), 2, "iftmp");

    PN->addIncoming(ThenV, ThenBB);
    PN->addIncoming(ElseV, ElseBB);
    return PN;
}

// Generate code for 'for/in' expressions
// With the introduction of for/in expressions, our symbol table can now contain function
// arguments or loop variables.
// It's possible that a var with the same name exists in outer scope,
// we choose to shadow the existing value in this case.
// Output for-loop as:
//   ...
//   start = startexpr
//   goto loop
// loop:
//   variable = phi [start, loopheader], [nextvariable, loopend]
//   ...
//   bodyexpr
//   ...
// loopend:
//   step = stepexpr
//   nextvariable = variable + step
//   endcond = endexpr
//   br endcond, loop, endloop
// outloop:
Value *ForExprAST::codegen() {
    Function *TheFunction = Builder.GetInsertBlock()->getParent();

    // Create an alloc for the variable in the entry block.
    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);

    // Emit debug location
    KSDbgInfo.emitLocation(this);

    // Emit the start code first, without 'variable in scope.
    Value *StartVal = Start->codegen();
    if (!StartVal)
        return nullptr;

    // Store the value into the alloca
    Builder.CreateStore(StartVal, Alloca);

    // Make the new basic block for the loop header, inserting after current block.
    BasicBlock *LoopBB = BasicBlock::Create(getGlobalContext(), "loop", TheFunction);

    // Insert an explicit fall through from the current block to the LoopBB
    Builder.CreateBr(LoopBB);

    // Start insertion in LoopBB.
    Builder.SetInsertPoint(LoopBB);

    // If the variable shadows an existing variable, we have to restore it, so save it now.
    AllocaInst *OldVal = NamedValues[VarName];
    NamedValues[VarName] = Alloca;

    // Emit the body of the loop. This, like any other expr, can change the current BB
    // Note that we ignore the value computed by the body, but don't allow an error.
    if (!Body->codegen())
        return nullptr;

    // Emit the step value.
    Value *StepVal = nullptr;
    if (Step) {
        StepVal = Step->codegen();
        if (!StepVal)
            return nullptr;
    } else {
        // If not specified, use 1.0
        StepVal = ConstantFP::get(getGlobalContext(), APFloat(1.0));
    }

    // Compute the end condition
    Value *EndCond = End->codegen();
    if (!EndCond)
        return nullptr;

    // Reload, increment and restore the alloca. This handles the case where
    // the body of the loop mutates the variable.
    Value *CurVar = Builder.CreateLoad(Alloca, VarName.c_str());
    Value *NextVar = Builder.CreateFAdd(CurVar, StepVal, "nextvar");
    Builder.CreateStore(NextVar, Alloca);

    // Convert condition to a bool by comparing equal to 0.0
    EndCond = Builder.CreateFCmpONE(EndCond,
            ConstantFP::get(getGlobalContext(), APFloat(0.0)), "loopcond");

    // Create the "after loop" block and insert it
    BasicBlock *AfterBB = BasicBlock::Create(getGlobalContext(), "afterloop", TheFunction);

    // Insert the conditional branch into the end of LoopEndBB
    Builder.CreateCondBr(EndCond, LoopBB, AfterBB);

    // And new code with be inserted in AfterBB.
    Builder.SetInsertPoint(AfterBB);

    // Restore the unshadowed variable
    if (OldVal) {
        NamedValues[VarName] = OldVal;
    } else {
        NamedValues.erase(VarName);
    }

    // for expr always returns 0.0
    return Constant::getNullValue(Type::getDoubleTy(getGlobalContext()));
}

// Generate code for unary expressions
Value *UnaryExprAST::codegen() {
    Value *OperandV = Operand->codegen();
    if (!OperandV)
        return nullptr;

    Function *F = getFunction(std::string("unary") + Opcode);
    if (!F)
        return ErrorV("Unknown unary operator");

    // Emit debug location
    KSDbgInfo.emitLocation(this);

    // Return function call
    return Builder.CreateCall(F, OperandV, "unop");
}

// Code generation for var/in expressions
Value *VarExprAST::codegen() {
    std::vector<AllocaInst *> OldBindings;

    Function *TheFunction = Builder.GetInsertBlock()->getParent();

    // Register all variables and emit their initializer.
    for (unsigned i = 0, e = VarNames.size(); i != e; ++i) {
        const std::string &VarName = VarNames[i].first;
        ExprAST *Init = VarNames[i].second.get();

        // Emit the initializer before adding the variable to scope, this prevents
        // the initializer from referencing the variable itself, and permits stuff like this:
        // var a = 1 in
        //   var a = a in ... # refers to outer 'a'
        Value *InitVal;
        if (Init) {
            InitVal = Init->codegen();
            if (!InitVal)
                return nullptr;
        } else { // if not specified, use 0.0
            InitVal = ConstantFP::get(getGlobalContext(), APFloat(0.0));
        }

        AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
        Builder.CreateStore(InitVal, Alloca);

        // Remember to old variable binding so that we can restore the binding when
        // we unrecurse.
        OldBindings.push_back(NamedValues[VarName]);

        // Remember this binding
        NamedValues[VarName] = Alloca;
    }

    // Emit debug location
    KSDbgInfo.emitLocation(this);

    // Codegen the body, now that all vars are in scope
    Value *BodyVal = Body->codegen();
    if (!BodyVal)
        return nullptr;

    // Pop all our variables from scope.
    for (unsigned i = 0, e = VarNames.size(); i != e; ++i) {
        NamedValues[VarNames[i].first] = OldBindings[i];
    }

    // Return the body computation
    return BodyVal;
}


// ================================================================
// Optimizer
// ================================================================

// Initializes the global module `TheModule`
void InitializeModule(void) {
    // Open a new module.
    TheModule = llvm::make_unique<Module>("dbeard jit", getGlobalContext());
    TheModule->setDataLayout(TheJIT->getTargetMachine().createDataLayout());
}


// ================================================================
// Top-Level parsing and JIT Driver
// ================================================================

static void HandleDefinition() {
    if (auto FnAST = Parser::ParseDefinition(lexer)) {
        if (!FnAST->codegen()) {
            fprintf(stderr, "Error reading function definition:");
        }
    } else {
        // Skip token for error recovery.
        lexer.getNextToken();
    }
}

static void HandleExtern() {
    if (auto ProtoAST = Parser::ParseExtern(lexer)) {
        if (!ProtoAST->codegen())
            fprintf(stderr, "Error reading extern");
        else
            FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    } else {
        // Skip token for error recovery.
        lexer.getNextToken();
    }
}

static void HandleTopLevelExpression() {
    // Evaluate a top-level expression into an anonymous function.
    if (auto FnAST = Parser::ParseTopLevelExpr(lexer)) {
        if (!FnAST->codegen())
            fprintf(stderr, "Error generating code for top level expression");
    } else {
        // Skip token for error recovery.
        lexer.getNextToken();
    }
}

// Driver invokes all of the parsing pieces with a top-level dispatch loop.
// Ignore top level semicolons.
// - Reason for this is so the parser knows whether that is the end of what you will type
// at the command line.
// - E.g. allows you to type 4+5; and the parser will know you are done.
// top ::= definition | external | expression | ';'
static void MainLoop() {
    while (1) {
        switch(lexer.getCurTok()) {
        case Lexer::tok_eof:
            return;
        case ';': // ignore top-level semicolons.
            lexer.getNextToken();
            break;
        case Lexer::tok_def:
            HandleDefinition();
            break;
        case Lexer::tok_extern:
            HandleExtern();
            break;
        default:
            HandleTopLevelExpression();
            break;
        }
    }
}

// ================================================================
// "Library" functions that can be "extern'd" from user code.
// ================================================================

// putchard - putchar that takes a double and returns 0.
extern "C" double putchard(double X) {
    fputc((char)X, stderr);
    return 0;
}

// printd - printf that takes a double and prints it as "%f\n", returning 0.
extern "C" double printd(double X) {
    fprintf(stderr, "%f\n", X);
    return 0;
}

// ================================================================
// Module loading code.
// ================================================================
static std::unique_ptr<Module> ParseInputIR(std::string InputFile) {
    SMDiagnostic Err;
    auto M = parseIRFile(InputFile, Err, getGlobalContext());
    if (!M) {
        Error("Problem parsing input IR");
        return nullptr;
    }

    M->setModuleIdentifier("IR:" + InputFile);
    return M;
}

// ================================================================
// Main Driver code.
// ================================================================

// Command line options
cl::OptionCategory
CompilerCategory("Compiler Options", "Options for controlling the compilation process.");
static cl::opt<std::string>
InputFilename("input-file", cl::desc("File to compile (defaults to stdin)"),
              cl::init("-"), cl::value_desc("filename"), cl::cat(CompilerCategory));
static cl::alias
InputFileAlias("i", cl::desc("Alias for -input-file"), cl::aliasopt(InputFilename));


static void handleCommandLineOptions() {
    // Open the file to compile.
    ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
    MemoryBuffer::getFileOrSTDIN(InputFilename);
    if (std::error_code EC = FileOrErr.getError()) {
        errs() << "Could not open input file '" << InputFilename
        << "': " << EC.message() << '\n';
        exit(2);
    }
    std::unique_ptr<MemoryBuffer> &File = FileOrErr.get();

    // Initialize the lexer with the source
    lexer = Lexer::Lexer(File->getBuffer().str());
}


int main(int argc, char **argv) {
    llvm::cl::HideUnrelatedOptions( CompilerCategory );
    llvm::cl::ParseCommandLineOptions(argc,argv);
    handleCommandLineOptions();


    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    // Install standard binary operators
    // 1 is lowest precendence
    Parser::BinopPrecedence['='] = 2;
    Parser::BinopPrecedence['<'] = 10;
    Parser::BinopPrecedence['+'] = 20;
    Parser::BinopPrecedence['-'] = 30;
    Parser::BinopPrecedence['*'] = 40; // Highest

    // Prime the first token.
    lexer.getNextToken();

    // Initialize the JIT
    TheJIT = llvm::make_unique<KaleidoscopeJIT>();

    // Setup the module
    InitializeModule();

    // Link in the stdlib
    auto M = ParseInputIR("lib/stdlib.ll");
    bool LinkErr = llvm::Linker::linkModules(*TheModule, std::move(M));
    if (LinkErr) {
        fprintf(stderr, "Error linking modules");
    }

    // Add the current debug info version into the module
    TheModule->addModuleFlag(Module::Warning, "Debug Info Version",
            DEBUG_METADATA_VERSION);

    // Darwin only supports dwarf2.
    if (Triple(sys::getProcessTriple()).isOSDarwin())
        TheModule->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 2);

    // Construct the DIBuilder, we do this here because we need the module.
    DBuilder = llvm::make_unique<DIBuilder>(*TheModule);

    // Create the compile unit for the module.
    // Currently down as fib.yk as a filename since we're redirecting stdin
    // but we'd like actual source locations.
    KSDbgInfo.TheCU = DBuilder->createCompileUnit(dwarf::DW_LANG_C, "fib.yk", ".",
            "Yorkie Compiler", 0, "", 0);

    // Run the main "interpreter loop" now.
    MainLoop();

    // Finalize the debug info.
    DBuilder->finalize();

    // Print out all of the generated code
    TheModule->dump();

    return 0;
}
