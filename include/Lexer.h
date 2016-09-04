
#ifndef YORKIE_LEXER_H
#define YORKIE_LEXER_H

#include <string>

//===============================================
// Lexer.h
//
// Breaks input up into tokens.
//
//===============================================

namespace Lexer {


// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
    tok_eof = -1,

    // commands
    tok_def = -2,
    tok_extern = -3,

    // primary
    tok_identifier = -4,
    tok_number = -5,

    // control flow
    tok_if = -6,
    tok_then = -7,
    tok_else = -8,
    tok_for = -9,
    tok_in = -10,

    // operators
    tok_binary = -11,
    tok_unary = -12,

    // var definition
    tok_var = -13,

    // end keyword
    tok_end = -14,
};

// Source Location Information
struct SourceLocation {
    int Line;
    int Col;
};

class Lexer {
    std::string SourceString;               // Contains the source.
    std::string::iterator SourceIterator;   // Source iterator
    SourceLocation LexLoc = {1, 0};         // Lexer source location
    std::string IdentifierStr;              // Filled in if tok_identifier
    double NumVal;                          // Filled in if tok_number
    // CurTok/getNextToken - Provide a simple token buffer. CurTok is the current
    // token the parser is looking at. getNextToken reads another token from the lexer
    // and updates CurTok with its results.
    int CurTok;

    // Private methods
    int advance();

public:

    // Constructors
    Lexer() {}
    Lexer(std::string source) : SourceString(source) {
        SourceIterator = SourceString.begin();
        //TODO: This should be moved.
        // Prime the first token.
        getNextToken();
    }

    // Accessors for private member declarations
    SourceLocation getLexLoc() { return LexLoc; }
    std::string getIdentifierStr() { return IdentifierStr; }
    double getNumVal() { return NumVal; }
    int getCurTok() { return CurTok; }

    // Public methods
    int gettok();           // Return the next token from standard input.
    int getNextToken();     // Allows us to look one token ahead at what the lexer is returning.
};

}

#endif /* end of include guard:  */
