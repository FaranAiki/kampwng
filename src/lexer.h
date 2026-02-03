#ifndef LEXER_H
#define LEXER_H

// --- TYPES ---

typedef enum {
  TOKEN_EOF,
  TOKEN_LOOP,   // loop
  TOKEN_LBRACKET, // [
  TOKEN_RBRACKET, // ]
  TOKEN_LBRACE,   // {
  TOKEN_RBRACE,   // }
  TOKEN_LPAREN,   // (
  TOKEN_RPAREN,   // )
  TOKEN_SEMICOLON,// ;
  TOKEN_COMMA,  // ,
  
  TOKEN_NUMBER,   // 10, 42
  TOKEN_FLOAT,  // 3.14, 0.5
  TOKEN_STRING,   // "Hello"
  TOKEN_CHAR_LIT, // 'a'
  TOKEN_IDENTIFIER, // myVar, x, count, print, input
  TOKEN_ASSIGN,   // =
  
  // Control Flow
  TOKEN_IF,     // if
  TOKEN_ELIF,   // elif
  TOKEN_ELSE,   // else
  TOKEN_RETURN,   // return

  // Primitive Types
  TOKEN_KW_VOID,   // void
  TOKEN_KW_INT,  // int
  TOKEN_KW_CHAR,   // char
  TOKEN_KW_BOOL,   // bool
  TOKEN_KW_SINGLE, // single (float)
  TOKEN_KW_DOUBLE, // double
  TOKEN_KW_LET,    // let (auto/inference)

  // Mutability
  TOKEN_KW_MUT,    // mut, mutable
  TOKEN_KW_IMUT,   // imut, immutable

  // Boolean Literals
  TOKEN_TRUE,    // true
  TOKEN_FALSE,   // false

  // Logical Operators
  TOKEN_NOT,     // ! or not

  // Arithmetic & Bitwise Operators
  TOKEN_PLUS,   // +
  TOKEN_MINUS,  // -
  TOKEN_STAR,   // *
  TOKEN_SLASH,  // /
  TOKEN_XOR,    // ^
  TOKEN_LSHIFT,   // <<
  TOKEN_RSHIFT,   // >>
  
  // Comparison Operators
  TOKEN_EQ,     // ==
  TOKEN_NEQ,    // !=
  TOKEN_LT,     // <
  TOKEN_GT,     // >
  TOKEN_LTE,    // <=
  TOKEN_GTE,    // >=
  
  TOKEN_UNKNOWN
} TokenType;

typedef struct {
  TokenType type;
  char *text;    // Stores the identifier name or string content
  int int_val;   // Stores integer value if type is NUMBER
  double double_val; // Stores float/double value if type is FLOAT
  int line;      // Line number
  int col;       // Column number
} Token;

typedef struct {
  const char *src;
  int pos;
  int line;
  int col;
} Lexer;

// --- PROTOTYPES ---

void lexer_init(Lexer *l, const char *src);
Token lexer_next(Lexer *l);
void free_token(Token t);

#endif
