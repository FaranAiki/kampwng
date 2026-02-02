#ifndef LEXER_H
#define LEXER_H

// --- TYPES ---

typedef enum {
    TOKEN_EOF,
    TOKEN_LOOP,     // loop
    TOKEN_PRINT,    // print
    TOKEN_LBRACKET, // [
    TOKEN_RBRACKET, // ]
    TOKEN_LBRACE,   // {
    TOKEN_RBRACE,   // }
    TOKEN_LPAREN,   // (
    TOKEN_RPAREN,   // )
    TOKEN_SEMICOLON,// ;
    TOKEN_NUMBER,   // 10, 42
    TOKEN_FLOAT,    // 3.14, 0.5
    TOKEN_STRING,   // "Hello"
    TOKEN_IDENTIFIER, // myVar, x, count
    TOKEN_ASSIGN,   // =
    
    // Control Flow
    TOKEN_IF,       // if
    TOKEN_ELIF,     // elif
    TOKEN_ELSE,     // else

    // Primitive Types
    TOKEN_KW_INT,    // int
    TOKEN_KW_CHAR,   // char
    TOKEN_KW_BOOL,   // bool
    TOKEN_KW_SINGLE, // single (float)
    TOKEN_KW_DOUBLE, // double

    // Boolean Literals
    TOKEN_TRUE,      // true
    TOKEN_FALSE,     // false

    // Arithmetic & Bitwise Operators
    TOKEN_PLUS,     // +
    TOKEN_MINUS,    // -
    TOKEN_STAR,     // *
    TOKEN_SLASH,    // /
    TOKEN_XOR,      // ^
    TOKEN_LSHIFT,   // <<
    TOKEN_RSHIFT,   // >>
    
    // Comparison Operators
    TOKEN_EQ,       // ==
    TOKEN_NEQ,      // !=
    TOKEN_LT,       // <
    TOKEN_GT,       // >
    TOKEN_LTE,      // <=
    TOKEN_GTE,      // >=
    
    TOKEN_UNKNOWN
} TokenType;

typedef struct {
    TokenType type;
    char *text;      // Stores the identifier name or string content
    int int_val;     // Stores integer value if type is NUMBER
    double double_val; // Stores float/double value if type is FLOAT
} Token;

typedef struct {
    const char *src;
    int pos;
} Lexer;

// --- PROTOTYPES ---

void lexer_init(Lexer *l, const char *src);
Token lexer_next(Lexer *l);
void free_token(Token t);

#endif
