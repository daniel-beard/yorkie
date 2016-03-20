
#include "Lexer.h"
#include <cstdlib>

int Lexer::Lexer::advance() {
    int LastChar = getchar();

    if (LastChar == '\n' || LastChar == '\r') {
        LexLoc.Line++;
        LexLoc.Col = 0;
    } else {
        LexLoc.Col++;
    }
    return LastChar;
}

/// gettok - Return the next token from standard input.
int Lexer::Lexer::gettok() {
    static int LastChar = ' ';

    // Skip any whitespace
    while (isspace(LastChar)) {
        LastChar = advance();
    }

    // Recognize identifiers and specific keywords like 'def'
    if (isalpha(LastChar)) { // identifier: [a-zA-Z][a-zA-Z0-9]*
        IdentifierStr = LastChar;
        while (isalnum((LastChar = advance()))) {
            IdentifierStr += LastChar;
        }

        if (IdentifierStr == "def")
            return tok_def;
        if (IdentifierStr == "extern")
            return tok_extern;
        if (IdentifierStr == "if")
            return tok_if;
        if (IdentifierStr == "then")
            return tok_then;
        if (IdentifierStr == "else")
            return tok_else;
        if (IdentifierStr == "for")
            return tok_for;
        if (IdentifierStr == "in")
            return tok_in;
        if (IdentifierStr == "binary")
            return tok_binary;
        if (IdentifierStr == "unary")
            return tok_unary;
        if (IdentifierStr == "var")
            return tok_var;
        if (IdentifierStr == "end")
            return tok_end;

        return tok_identifier;
    }

    // Handle Numeric Values
    // Naive, won't handle things like 1.1.1 etc.
    if (isdigit(LastChar) || LastChar == '.') { // Number: [0-9.]+
        std::string NumStr;
        do {
            NumStr += LastChar;
            LastChar = advance();
        } while (isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(), 0);
        return tok_number;
    }

    // Handle comments
    // We skip to the end of the line then return the next token
    if (LastChar == '#') {
        // Comment until end of line.
        do {
            LastChar = advance();
        } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar != EOF) {
            return gettok();
        }
    }

    // If the input doesn't match one of the above cases, it is either an operator
    // character like '+' or the end of file. Handle these below.

    // Check for end of file. Don't eat the EOF
    if (LastChar == EOF) {
        return tok_eof;
    }

    // Otherwise, just return the character as its ascii value.
    int ThisChar = LastChar;
    LastChar = advance();
    return ThisChar;
}

// Allows us to look one token ahead at what the lexer is returning.
int Lexer::Lexer::getNextToken() {
    return CurTok = gettok();
}
