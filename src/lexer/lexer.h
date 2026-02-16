#ifndef LEXER_H
#define LEXER_H

typedef enum {
  TOKEN_EOF,
  TOKEN_LOOP,   
  TOKEN_WHILE,  
  TOKEN_ONCE,   
  TOKEN_LBRACKET, 
  TOKEN_RBRACKET, 
  TOKEN_LBRACE,   
  TOKEN_RBRACE,   
  TOKEN_LPAREN,   
  TOKEN_RPAREN,   
  TOKEN_SEMICOLON,
  TOKEN_COLON,    // Added ':'
  TOKEN_COMMA,  
  TOKEN_ELLIPSIS, 
  TOKEN_DOT,    
  
  TOKEN_NUMBER,   
  TOKEN_FLOAT,  
  TOKEN_STRING,
  TOKEN_C_STRING, // Added: c"..."
  TOKEN_CHAR_LIT, 
  TOKEN_IDENTIFIER, 
  
  // Assignment
  TOKEN_ASSIGN,       // =
  TOKEN_PLUS_ASSIGN,  // +=
  TOKEN_MINUS_ASSIGN, // -=
  TOKEN_STAR_ASSIGN,  // *=
  TOKEN_SLASH_ASSIGN, // /=
  TOKEN_MOD_ASSIGN,   // %=
  TOKEN_AND_ASSIGN,   // &=
  TOKEN_OR_ASSIGN,    // |=
  TOKEN_XOR_ASSIGN,   // ^=
  TOKEN_LSHIFT_ASSIGN,// <<=
  TOKEN_RSHIFT_ASSIGN,// >>=
  
  TOKEN_IF,     
  TOKEN_ELIF, 
  TOKEN_THEN, // Added THEN
  TOKEN_ELSE,   
  TOKEN_RETURN,
  TOKEN_BREAK,
  TOKEN_CONTINUE,
  TOKEN_SWITCH,   // Added
  TOKEN_CASE,     // Added
  TOKEN_DEFAULT,  // Added
  TOKEN_LEAK,     // Added
  
  TOKEN_DEFINE, 
  TOKEN_AS,     
  TOKEN_TYPEDEF, 

  // OOP Keywords
  TOKEN_CLASS,
  TOKEN_STRUCT, // Added struct keyword
  TOKEN_UNION,  // Added union keyword
  TOKEN_IS,     
  TOKEN_HAS,    
  TOKEN_OPEN,   
  TOKEN_CLOSED, 
  TOKEN_TYPEOF,
  TOKEN_HASMETHOD,    // New
  TOKEN_HASATTRIBUTE, // New

  TOKEN_NAMESPACE, 
  TOKEN_ENUM, 

  // Flux / Generator Support
  TOKEN_FLUX,
  TOKEN_EMIT,
  TOKEN_FOR,
  TOKEN_IN,

  TOKEN_KW_VOID,   
  TOKEN_KW_INT,  
  TOKEN_KW_CHAR,   
  TOKEN_KW_BOOL,   
  TOKEN_KW_SINGLE, 
  TOKEN_KW_DOUBLE, 
  TOKEN_KW_STRING, // Added for 'string' type
  TOKEN_KW_LET,    

  TOKEN_KW_SHORT,
  TOKEN_KW_LONG,
  TOKEN_KW_UNSIGNED,

  // Extended Literal Tokens
  TOKEN_ULONG_LONG_LIT,
  TOKEN_LONG_LONG_LIT,
  TOKEN_ULONG_LIT,
  TOKEN_LONG_LIT,
  TOKEN_UINT_LIT,
  TOKEN_LONG_DOUBLE_LIT,

  TOKEN_KW_MUT,    
  TOKEN_KW_IMUT,   

  TOKEN_IMPORT,    
  TOKEN_EXTERN,    
  TOKEN_LINK,      

  TOKEN_TRUE,    
  TOKEN_FALSE,   

  TOKEN_NOT,     // !
  TOKEN_BIT_NOT, // ~

  TOKEN_PLUS,    // +
  TOKEN_INCREMENT, // ++
  TOKEN_MINUS,   // -
  TOKEN_DECREMENT, // --
  TOKEN_STAR,    // *
  TOKEN_SLASH,   // /
  TOKEN_MOD,     // %
  TOKEN_AND,     // &
  TOKEN_OR,      // |
  TOKEN_XOR,     // ^
  TOKEN_LSHIFT,  // <<
  TOKEN_RSHIFT,  // >>
  
  TOKEN_AND_AND, // &&
  TOKEN_OR_OR,   // ||
  
  TOKEN_EQ,     
  TOKEN_NEQ,    
  TOKEN_LT,     
  TOKEN_GT,     
  TOKEN_LTE,    
  TOKEN_GTE,    
  
  TOKEN_UNKNOWN
} TokenType;

typedef struct {
  TokenType type;
  char *text;    
  int int_val;   
  unsigned long long long_val; // Added for extended ints
  double double_val; 
  int line;      
  int col;       
} Token;

typedef struct {
  const char *src;
  const char *filename; // Added for diagnostic context
  int pos;
  int line;
  int col;
} Lexer;

void lexer_init(Lexer *l, const char *src);
Token lexer_next(Lexer *l);
void free_token(Token t);

void skip_whitespace_and_comments(Lexer *l);
char *consume_string_content(Lexer *l);

int lex_symbol(Lexer *l, Token *t);
int lex_number(Lexer *l, Token *t);
int lex_char(Lexer *l, Token *t);
int lex_string(Lexer *l, Token *t);
int lex_word(Lexer *l, Token *t);

#endif
