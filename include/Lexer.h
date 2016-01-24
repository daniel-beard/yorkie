
#ifndef YORKIE_LEXER_H
#define YORKIE_LEXER_H

namespace Lexer {
// http://llvm.org/docs/tutorial/LangImpl1.html#the-lexer
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


}

#endif /* end of include guard:  */
