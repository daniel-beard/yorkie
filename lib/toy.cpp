
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

using namespace llvm;
using namespace llvm::orc;

// Lexer
static Lexer::Lexer lexer = Lexer::Lexer();

// IR Builder.
static IRBuilder<> Builder(getGlobalContext());

struct DebugInfo {
    DICompileUnit *TheCU;
    DIType *DblTy;
    std::vector<DIScope *> LexicalBlocks;

    void emitLocation(ExprAST *AST);
    DIType *getDoubleTy();
} KSDbgInfo;

// ================================================================
// Parser
// ================================================================

// Method definitions
static std::unique_ptr<ExprAST> ParsePrimary();
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS);
static std::unique_ptr<ExprAST> ParseIfExpr();
static std::unique_ptr<ExprAST> ParseForExpr();
static std::unique_ptr<ExprAST> ParseUnary();
static std::unique_ptr<ExprAST> ParseVarExpr();

// Error* - These are little helper functions for error handling.
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

// Expression parsing

// To start, an expression is a primary expression potentially followed by a sequence of
// [binop, primaryexpr] pairs.
// expression
//  ::= primary binoprhs
static std::unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParseUnary();
    if (!LHS)
        return nullptr;

    return ParseBinOpRHS(0, std::move(LHS));
}

// Numeric literals
// Expects to be called when the current token is a `tok_number`
// Takes the current number and creates a `NumberExprAST` node, advances to the next token
// and returns.
// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = llvm::make_unique<NumberExprAST>(lexer.getLexLoc(), lexer.getNumVal());
    lexer.getNextToken(); // consume the number
    return std::move(Result);
}

// Parenthesis Operator
// Demonstrates error routines, expects that the current token is `(`, but there may not be
// a corresponding `)` token.
// We return null on an error.
// We recursively call ParseExpression, this is powerful because we can handle recursive grammars.
// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
    lexer.getNextToken(); // eat (.
    auto V = ParseExpression();
    if (!V)
        return nullptr;

    if (lexer.getCurTok() != ')')
        return Error("expected ')'");
    lexer.getNextToken(); // eat ).
    return V;
}

// Variable references and function calls
// Expects to be called if the current token is `tok_identifier`
// Uses look-ahead to determine if the current identifier is a stand alone var reference
// or if it is a function call expression.
// identifierexpr
//  ::= identifier
//  ::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIndentifierExpr() {
    std::string IdName = lexer.getIdentifierStr();

    lexer.getNextToken(); // eat identifier

    if (lexer.getCurTok() != '(') // Simple variable ref
        return llvm::make_unique<VariableExprAST>(lexer.getLexLoc(), IdName);

    // Call.
    lexer.getNextToken(); // Eat (
    std::vector<std::unique_ptr<ExprAST>> Args;
    if (lexer.getCurTok() != ')') {
        while (1) {
            if (auto Arg = ParseExpression()) {
                Args.push_back(std::move(Arg));
            } else {
                return nullptr;
            }

            if (lexer.getCurTok() == ')')
                break;

            if (lexer.getCurTok() != ',')
                return Error("Expected ')' or ',' in argument list");
            lexer.getNextToken(); // eat the ','
        }
    }

    // Eat the ')'.
    lexer.getNextToken();

    return llvm::make_unique<CallExprAST>(lexer.getLexLoc(), IdName, std::move(Args));
}

// primary
//  ::= identifierexpr
//  ::= numberexpr
//  ::= parenexpr
//  ::= ifexpr
//  ::= forexpr
//  ::= varexpr
static std::unique_ptr<ExprAST> ParsePrimary() {
    switch (lexer.getCurTok()) {
        default:
            return Error("unknown token when expecting an expression");
        case Lexer::tok_identifier:
            return ParseIndentifierExpr();
        case Lexer::tok_number:
            return ParseNumberExpr();
        case '(':
            return ParseParenExpr();
        case Lexer::tok_if:
            return ParseIfExpr();
        case Lexer::tok_for:
            return ParseForExpr();
        case Lexer::tok_var:
            return ParseVarExpr();
    }
}

// Handle binary operator precedence
// https://en.wikipedia.org/wiki/Operator-precedence_parser

// BinopPrecendence - This holds the precendence for each binary operator that is defined.
static std::map<char, int> BinopPrecedence;

// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
    if (!isascii(lexer.getCurTok()))
        return -1;

    // Make sure it's a declared binop.
    int TokPrec = BinopPrecedence[lexer.getCurTok()];
    if (TokPrec <= 0) return -1;
    return TokPrec;
}

// Parse sequences of pairs. Takes a precendence and a pointer to an expression for the
// part that has been parsed so far.
// The precendence passed into `ParseBinOpRHS` indicates the minimal operator precendence that
// the function is allowed to eat.
// binoprhs
//  ::= ('+' primary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS) {
    // if this is a binop, find its precendence
    while (1) {
        int TokPrec = GetTokPrecedence();

        // If this is a binop that binds at least as tightly as the current binop,
        // consume it, otherwise we are done.
        if (TokPrec < ExprPrec)
            return LHS;

        // Okay, we know this is a binop.
        int BinOp = lexer.getCurTok();
        Lexer::SourceLocation BinLoc = lexer.getLexLoc();
        lexer.getNextToken(); // eat binop

        // Parse the unary expression after the binary operator
        auto RHS = ParseUnary();
        if (!RHS)
            return nullptr;

        // If BinOp binds less tightly with RHS than the operator after RHS, let
        // the pending operator take RHS as its LHS
        int NextPrec = GetTokPrecedence();
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec+1, std::move(RHS));
            if (!RHS)
                return nullptr;
        }
        // Merge LHS/RHS
        LHS = llvm::make_unique<BinaryExprAST>(BinLoc, BinOp, std::move(LHS), std::move(RHS));
    } // loop around to the top of the while loop
}

// Handle function prototypes, used for 'extern' function declarations as well as function
// body definitions.
// prototype
//  ::= id '(' id* ')'
//  ::= binary LETTER number? (id, id)
static std::unique_ptr<PrototypeAST> ParsePrototype() {
    std::string FnName;

    Lexer::SourceLocation FnLoc = lexer.getLexLoc();

    unsigned Kind = 0; // 0 = identifier, 1 = unary, 2 = binary.
    unsigned BinaryPrecedence = 30;

    switch (lexer.getCurTok()) {
    default:
        return ErrorP("Expected function name in prototype");
    case Lexer::tok_identifier:
        FnName = lexer.getIdentifierStr();
        Kind = 0;
        lexer.getNextToken(); // eat identifier
        break;
    case Lexer::tok_unary:
        lexer.getNextToken(); // eat 'unary'
        if (!isascii(lexer.getCurTok()))
            return ErrorP("Expected unary operator");
        FnName = "unary";
        FnName += (char)lexer.getCurTok();
        Kind = 1;
        lexer.getNextToken(); // eat ascii operator
        break;
    case Lexer::tok_binary:
        lexer.getNextToken(); // eat 'binary'
        if (!isascii(lexer.getCurTok()))
            return ErrorP("Expected ascii binary operator");
        FnName = "binary";
        FnName += (char)lexer.getCurTok();
        Kind = 2;
        lexer.getNextToken(); // eat ascii operator

        // Read the precedence if present
        if (lexer.getCurTok() == Lexer::tok_number) {
            if (lexer.getNumVal() < 1 || lexer.getNumVal() > 100)
                return ErrorP("Invalid precedence: must be 1..100");
            BinaryPrecedence = (unsigned)lexer.getNumVal();
            lexer.getNextToken(); // eat precedence
        }
        break;
    }

    if (lexer.getCurTok() != '(')
        return ErrorP("Expected '(' in prototype");

    // Read list of argument names
    std::vector<std::string> ArgNames;
    while (lexer.getNextToken() == Lexer::tok_identifier) {
        ArgNames.push_back(lexer.getIdentifierStr());
    }
    if (lexer.getCurTok() != ')')
        return ErrorP("Expected ')' in prototype");

    // success
    lexer.getNextToken(); // eat ')'

    // Verify right number of names for operator.
    if (Kind > 0 && ArgNames.size() != Kind)
        return ErrorP("Invalid number of operands for operator");

    return llvm::make_unique<PrototypeAST>(FnLoc, FnName, ArgNames, Kind != 0,
            BinaryPrecedence);
}

// Function definition, just a prototype plus an expression to implement the body
// definition ::= 'def' prototype expression 'end'
static std::unique_ptr<FunctionAST> ParseDefinition() {
    lexer.getNextToken(); // eat def.
    auto Proto = ParsePrototype();
    if (!Proto) return nullptr;

    auto E = ParseExpression();
    if (!E)
        return nullptr;

    if (lexer.getCurTok() != Lexer::tok_end)
        return ErrorF("expected 'end' after function definition");

    lexer.getNextToken(); // eat 'end'

    return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(E));
}

// Support extern to declare functions like 'sin' and 'cos' as well as to support
// forward declarations of user functions. These are just prototypes with no body.
// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
    lexer.getNextToken(); // eat extern.
    return ParsePrototype();
}

// Arbitrary top level expressions and evaluate on the fly.
// Will handle this by defining anonymous nullary (zero argument) functions for them
// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    Lexer::SourceLocation FnLoc = lexer.getLexLoc();
    if (auto E = ParseExpression()) {
        // Make anonymous proto
        auto Proto = llvm::make_unique<PrototypeAST>(FnLoc, "main", std::vector<std::string>());
        return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

// If expression parsing
// ifexpr ::= 'if' expression 'then' expression 'else' expression 'end'
static std::unique_ptr<ExprAST> ParseIfExpr() {
    lexer.getNextToken(); // eat the if

    // condition
    auto Cond = ParseExpression();
    if (!Cond)
        return nullptr;

    if (lexer.getCurTok() != Lexer::tok_then)
        return Error("expected then");
    lexer.getNextToken(); // eat the then

    auto Then = ParseExpression();
    if (!Then)
        return nullptr;

    if (lexer.getCurTok() != Lexer::tok_else)
        return Error("expected else");
    lexer.getNextToken(); // eat the else

    auto Else = ParseExpression();
    if (!Else)
        return nullptr;

    if (lexer.getCurTok() != Lexer::tok_end)
        return Error("expected 'end' after if expression");
    lexer.getNextToken(); // eat 'end'

    return llvm::make_unique<IfExprAST>(lexer.getLexLoc(), std::move(Cond), std::move(Then), std::move(Else));
}

// For expression parsing
// The step value is optional
// forexpr ::= 'for' identifier '=' expr ',' expr (',' expr)? 'in' expression
static std::unique_ptr<ExprAST> ParseForExpr() {
    lexer.getNextToken(); // eat the for.

    if (lexer.getCurTok() != Lexer::tok_identifier)
        return Error("expected identifier after for");

    std::string IdName = lexer.getIdentifierStr();
    lexer.getNextToken(); // eat identifier

    if (lexer.getCurTok() != '=')
        return Error("expected '=' after for");
    lexer.getNextToken(); // eat '='

    auto Start = ParseExpression();
    if (!Start)
        return nullptr;
    if (lexer.getCurTok() != ',')
        return Error("expected ',' after for start value");
    lexer.getNextToken(); // eat ','

    auto End = ParseExpression();
    if (!End)
        return nullptr;

    // The step value is optional
    std::unique_ptr<ExprAST> Step;
    if (lexer.getCurTok() == ',') {
        lexer.getNextToken(); // eat ','
        Step = ParseExpression();
        if (!Step)
            return nullptr;
    }

    if (lexer.getCurTok() != Lexer::tok_in)
        return Error("expected 'in' after for");
    lexer.getNextToken(); // eat 'in'.

    auto Body = ParseExpression();
    if (!Body)
        return nullptr;

    if (lexer.getCurTok() != Lexer::tok_end)
        return Error("expected 'end' after for");
    lexer.getNextToken(); // eat 'end'

    return llvm::make_unique<ForExprAST>(lexer.getLexLoc(), IdName, std::move(Start),
            std::move(End), std::move(Step), std::move(Body));
}

// Parse unary expression
// If we see a unary operator when parsing a primary operator, eat the operator and parse
// the remaining piece as another unary operator.
// This lets us handle multiple unary operators (e.g. '!!x')
// Unary operators aren't ambiguous, so no need for precedence.
// unary
//  ::= primary
//  ::= '!' unary
static std::unique_ptr<ExprAST> ParseUnary() {
    // If the current token is not an operator, it must be a primary expr.
    if (!isascii(lexer.getCurTok()) || lexer.getCurTok() == '(' || lexer.getCurTok() == ',')
        return ParsePrimary();

    // If this is a unary operator, read it.
    int Opc = lexer.getCurTok();
    lexer.getNextToken(); // eat unary operator
    if (auto Operand = ParseUnary())
        return llvm::make_unique<UnaryExprAST>(lexer.getLexLoc(), Opc, std::move(Operand));
    return nullptr;
}

// varexpr ::= 'var' identifier ('=' expression)?
//                  (',' identifier ('=' expression)?* 'in' expression 'end'
static std::unique_ptr<ExprAST> ParseVarExpr() {
    lexer.getNextToken(); // eat the 'var'.

    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;

    // At least on variable name is required.
    if (lexer.getCurTok() != Lexer::tok_identifier)
        return Error("expected identifier after var");

    // Parse the list of identifier/expr pairs into the local `VarNames` vector.
    while (1) {
        std::string Name = lexer.getIdentifierStr();
        lexer.getNextToken(); // eat identifier

        // Read the optional initializer.
        std::unique_ptr<ExprAST> Init;
        if (lexer.getCurTok() == '=') {
            lexer.getNextToken(); // eat the '='

            Init = ParseExpression();
            if (!Init)
                return nullptr;
        }

        VarNames.push_back(std::make_pair(Name, std::move(Init)));

        // End of var list, exit loop.
        if (lexer.getCurTok() != ',')
            break;

        lexer.getNextToken(); // eat the ','

        if (lexer.getCurTok() != Lexer::tok_identifier)
            return Error("expected identifier list after var");
    }

    // At this point we have to have 'in'.
    if (lexer.getCurTok() != Lexer::tok_in)
        return Error("expected 'in' keyword after 'var'");
    lexer.getNextToken(); // eat 'in'.

    auto Body = ParseExpression();
    if (!Body)
        return nullptr;

    if (lexer.getCurTok() != Lexer::tok_end)
        return Error("expected 'end' after 'var'");
    lexer.getNextToken(); // eat 'end'

    return llvm::make_unique<VarExprAST>(lexer.getLexLoc(), std::move(VarNames), std::move(Body));
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
        BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

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
        BinopPrecedence.erase(Proto->getOperatorName());

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
    if (auto FnAST = ParseDefinition()) {
        if (!FnAST->codegen()) {
            fprintf(stderr, "Error reading function definition:");
        }
    } else {
        // Skip token for error recovery.
        lexer.getNextToken();
    }
}

static void HandleExtern() {
    if (auto ProtoAST = ParseExtern()) {
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
    if (auto FnAST = ParseTopLevelExpr()) {
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

int main() {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    // Install standard binary operators
    // 1 is lowest precendence
    BinopPrecedence['='] = 2;
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 30;
    BinopPrecedence['*'] = 40; // Highest

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
