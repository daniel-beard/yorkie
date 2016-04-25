
#include "Parser.h"
#include "Lexer.h"
#include "AST.h"
#include "Utils.h"

namespace Parser {

// =============================================================================
// Private Functions
// =============================================================================

// GetTokPrecedence - Get the precedence of the pending binary operator token.
int GetTokPrecedence(Lexer::Lexer &lexer) {
    if (!isascii(lexer.getCurTok()))
        return -1;

    // Make sure it's a declared binop.
    int TokPrec = BinopPrecedence[lexer.getCurTok()];
    if (TokPrec <= 0) return -1;
    return TokPrec;
}

// =============================================================================
// Parsing
// =============================================================================

// Parse sequences of pairs. Takes a precendence and a pointer to an expression for the
// part that has been parsed so far.
// The precendence passed into `ParseBinOpRHS` indicates the minimal operator precendence that
// the function is allowed to eat.
// binoprhs
//  ::= ('+' primary)*
std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS, Lexer::Lexer &lexer) {
    // if this is a binop, find its precendence
    while (1) {
        int TokPrec = GetTokPrecedence(lexer);

        // If this is a binop that binds at least as tightly as the current binop,
        // consume it, otherwise we are done.
        if (TokPrec < ExprPrec)
            return LHS;

        // Okay, we know this is a binop.
        int BinOp = lexer.getCurTok();
        Lexer::SourceLocation BinLoc = lexer.getLexLoc();
        lexer.getNextToken(); // eat binop

        // Parse the unary expression after the binary operator
        auto RHS = ParseUnary(lexer);
        if (!RHS)
            return nullptr;

        // If BinOp binds less tightly with RHS than the operator after RHS, let
        // the pending operator take RHS as its LHS
        int NextPrec = GetTokPrecedence(lexer);
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec+1, std::move(RHS), lexer);
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
std::unique_ptr<PrototypeAST> ParsePrototype(Lexer::Lexer &lexer) {
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
std::unique_ptr<FunctionAST> ParseDefinition(Lexer::Lexer &lexer) {
    lexer.getNextToken(); // eat def.
    auto Proto = ParsePrototype(lexer);
    if (!Proto) return nullptr;

    auto E = ParseExpression(lexer);
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
std::unique_ptr<PrototypeAST> ParseExtern(Lexer::Lexer &lexer) {
    lexer.getNextToken(); // eat extern.
    return ParsePrototype(lexer);
}

// Arbitrary top level expressions and evaluate on the fly.
// Will handle this by defining anonymous nullary (zero argument) functions for them
// toplevelexpr ::= expression
std::unique_ptr<FunctionAST> ParseTopLevelExpr(Lexer::Lexer &lexer) {
    Lexer::SourceLocation FnLoc = lexer.getLexLoc();
    if (auto E = ParseExpression(lexer)) {
        // Make anonymous proto
        auto Proto = llvm::make_unique<PrototypeAST>(FnLoc, "main", std::vector<std::string>());
        return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

// Parse unary expression
// If we see a unary operator when parsing a primary operator, eat the operator and parse
// the remaining piece as another unary operator.
// This lets us handle multiple unary operators (e.g. '!!x')
// Unary operators aren't ambiguous, so no need for precedence.
// unary
//  ::= primary
//  ::= '!' unary
std::unique_ptr<ExprAST> ParseUnary(Lexer::Lexer &lexer) {
    // If the current token is not an operator, it must be a primary expr.
    if (!isascii(lexer.getCurTok()) || lexer.getCurTok() == '(' || lexer.getCurTok() == ',')
        return ParsePrimary(lexer);

    // If this is a unary operator, read it.
    int Opc = lexer.getCurTok();
    lexer.getNextToken(); // eat unary operator
    if (auto Operand = ParseUnary(lexer))
        return llvm::make_unique<UnaryExprAST>(lexer.getLexLoc(), Opc, std::move(Operand));
    return nullptr;
}

// To start, an expression is a primary expression potentially followed by a sequence of
// [binop, primaryexpr] pairs.
// expression
//  ::= primary binoprhs
std::unique_ptr<ExprAST> ParseExpression(Lexer::Lexer &lexer) {
    auto LHS = ParseUnary(lexer);
    if (!LHS)
        return nullptr;

    return ParseBinOpRHS(0, std::move(LHS), lexer);
}

// varexpr ::= 'var' identifier ('=' expression)?
//                  (',' identifier ('=' expression)?* 'in' expression 'end'
std::unique_ptr<ExprAST> ParseVarExpr(Lexer::Lexer &lexer) {
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

            Init = ParseExpression(lexer);
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

    auto Body = ParseExpression(lexer);
    if (!Body)
        return nullptr;

    if (lexer.getCurTok() != Lexer::tok_end)
        return Error("expected 'end' after 'var'");
    lexer.getNextToken(); // eat 'end'

    return llvm::make_unique<VarExprAST>(lexer.getLexLoc(), std::move(VarNames), std::move(Body));
}

// For expression parsing
// The step value is optional
// forexpr ::= 'for' identifier '=' expr ',' expr (',' expr)? 'in' expression
std::unique_ptr<ExprAST> ParseForExpr(Lexer::Lexer &lexer) {
    lexer.getNextToken(); // eat the for.

    if (lexer.getCurTok() != Lexer::tok_identifier)
        return Error("expected identifier after for");

    std::string IdName = lexer.getIdentifierStr();
    lexer.getNextToken(); // eat identifier

    if (lexer.getCurTok() != '=')
        return Error("expected '=' after for");
    lexer.getNextToken(); // eat '='

    auto Start = ParseExpression(lexer);
    if (!Start)
        return nullptr;
    if (lexer.getCurTok() != ',')
        return Error("expected ',' after for start value");
    lexer.getNextToken(); // eat ','

    auto End = ParseExpression(lexer);
    if (!End)
        return nullptr;

    // The step value is optional
    std::unique_ptr<ExprAST> Step;
    if (lexer.getCurTok() == ',') {
        lexer.getNextToken(); // eat ','
        Step = ParseExpression(lexer);
        if (!Step)
            return nullptr;
    }

    if (lexer.getCurTok() != Lexer::tok_in)
        return Error("expected 'in' after for");
    lexer.getNextToken(); // eat 'in'.

    auto Body = ParseExpression(lexer);
    if (!Body)
        return nullptr;

    if (lexer.getCurTok() != Lexer::tok_end)
        return Error("expected 'end' after for");
    lexer.getNextToken(); // eat 'end'

    return llvm::make_unique<ForExprAST>(lexer.getLexLoc(), IdName, std::move(Start),
            std::move(End), std::move(Step), std::move(Body));
}

// If expression parsing
// ifexpr ::= 'if' expression 'then' expression 'else' expression 'end'
std::unique_ptr<ExprAST> ParseIfExpr(Lexer::Lexer &lexer) {
    lexer.getNextToken(); // eat the if

    // condition
    auto Cond = ParseExpression(lexer);
    if (!Cond)
        return nullptr;

    if (lexer.getCurTok() != Lexer::tok_then)
        return Error("expected then");
    lexer.getNextToken(); // eat the then

    auto Then = ParseExpression(lexer);
    if (!Then)
        return nullptr;

    if (lexer.getCurTok() != Lexer::tok_else)
        return Error("expected else");
    lexer.getNextToken(); // eat the else

    auto Else = ParseExpression(lexer);
    if (!Else)
        return nullptr;

    if (lexer.getCurTok() != Lexer::tok_end)
        return Error("expected 'end' after if expression");
    lexer.getNextToken(); // eat 'end'

    return llvm::make_unique<IfExprAST>(lexer.getLexLoc(), std::move(Cond), std::move(Then), std::move(Else));
}

// Parenthesis Operator
// Demonstrates error routines, expects that the current token is `(`, but there may not be
// a corresponding `)` token.
// We return null on an error.
// We recursively call ParseExpression, this is powerful because we can handle recursive grammars.
// parenexpr ::= '(' expression ')'
std::unique_ptr<ExprAST> ParseParenExpr(Lexer::Lexer &lexer) {
    lexer.getNextToken(); // eat (.
    auto V = ParseExpression(lexer);
    if (!V)
        return nullptr;

    if (lexer.getCurTok() != ')')
        return Error("expected ')'");
    lexer.getNextToken(); // eat ).
    return V;
}

// Numeric literals
// Expects to be called when the current token is a `tok_number`
// Takes the current number and creates a `NumberExprAST` node, advances to the next token
// and returns.
// numberexpr ::= number
std::unique_ptr<ExprAST> ParseNumberExpr(Lexer::Lexer &lexer) {
    auto Result = llvm::make_unique<NumberExprAST>(lexer.getLexLoc(), lexer.getNumVal());
    lexer.getNextToken(); // consume the number
    return std::move(Result);
}

// Variable references and function calls
// Expects to be called if the current token is `tok_identifier`
// Uses look-ahead to determine if the current identifier is a stand alone var reference
// or if it is a function call expression.
// identifierexpr
//  ::= identifier
//  ::= identifier '(' expression* ')'
std::unique_ptr<ExprAST> ParseIndentifierExpr(Lexer::Lexer &lexer) {
    std::string IdName = lexer.getIdentifierStr();

    lexer.getNextToken(); // eat identifier

    if (lexer.getCurTok() != '(') // Simple variable ref
        return llvm::make_unique<VariableExprAST>(lexer.getLexLoc(), IdName);

    // Call.
    lexer.getNextToken(); // Eat (
    std::vector<std::unique_ptr<ExprAST>> Args;
    if (lexer.getCurTok() != ')') {
        while (1) {
            if (auto Arg = ParseExpression(lexer)) {
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
std::unique_ptr<ExprAST> ParsePrimary(Lexer::Lexer &lexer) {
    switch (lexer.getCurTok()) {
        default:
            return Error("unknown token when expecting an expression");
        case Lexer::tok_identifier:
            return ParseIndentifierExpr(lexer);
        case Lexer::tok_number:
            return ParseNumberExpr(lexer);
        case '(':
            return ParseParenExpr(lexer);
        case Lexer::tok_if:
            return ParseIfExpr(lexer);
        case Lexer::tok_for:
            return ParseForExpr(lexer);
        case Lexer::tok_var:
            return ParseVarExpr(lexer);
    }
}

} // Namespace
