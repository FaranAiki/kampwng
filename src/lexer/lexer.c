#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

void lexer_init(Lexer *l, const char *src) {
  l->src = src;
  l->pos = 0;
  l->line = 1;
  l->col = 1;
}

char peek(Lexer *l) { return l->src[l->pos]; }

char advance(Lexer *l) {
  char c = l->src[l->pos];
  if (c == '\0') return c;
  
  l->pos++;
  if (c == '\n') {
    l->line++;
    l->col = 1;
  } else {
    l->col++;
  }
  return c;
}

Token lexer_next(Lexer *l) {
  Token t = {TOKEN_UNKNOWN, NULL, 0, 0.0, l->line, l->col};
  
  while (1) {
    char c = peek(l);
    if (c == '\0') break;

    if (isspace(c)) {
      advance(l);
      continue;
    }

    if (c == '/' && l->src[l->pos + 1] == '/') {
      while (peek(l) != '\0' && peek(l) != '\n') {
        advance(l);
      }
      continue; 
    }

    break;
  }

  t.line = l->line;
  t.col = l->col;

  char c = peek(l);

  if (c == '\0') {
    t.type = TOKEN_EOF;
    return t;
  }

  // --- Symbols & Operators ---

  if (c == '.') {
      if (l->src[l->pos+1] == '.' && l->src[l->pos+2] == '.') {
          advance(l); advance(l); advance(l);
          t.type = TOKEN_ELLIPSIS;
          return t;
      }
  }

  if (c == ',') { advance(l); t.type = TOKEN_COMMA; return t; }
  if (c == '[') { advance(l); t.type = TOKEN_LBRACKET; return t; }
  if (c == ']') { advance(l); t.type = TOKEN_RBRACKET; return t; }
  if (c == '{') { advance(l); t.type = TOKEN_LBRACE; return t; }
  if (c == '}') { advance(l); t.type = TOKEN_RBRACE; return t; }
  if (c == '(') { advance(l); t.type = TOKEN_LPAREN; return t; }
  if (c == ')') { advance(l); t.type = TOKEN_RPAREN; return t; }
  if (c == ';') { advance(l); t.type = TOKEN_SEMICOLON; return t; }
  if (c == '~') { advance(l); t.type = TOKEN_BIT_NOT; return t; }

  // Compound Operators & Double Char Operators

  if (c == '=') { 
    advance(l); 
    if (peek(l) == '=') { advance(l); t.type = TOKEN_EQ; return t; }
    t.type = TOKEN_ASSIGN; return t; 
  }

  if (c == '+') {
    advance(l);
    if (peek(l) == '=') { advance(l); t.type = TOKEN_PLUS_ASSIGN; return t; }
    if (peek(l) == '+') { advance(l); t.type = TOKEN_INCREMENT; return t; }
    t.type = TOKEN_PLUS; return t;
  }

  if (c == '-') {
    advance(l);
    if (peek(l) == '=') { advance(l); t.type = TOKEN_MINUS_ASSIGN; return t; }
    if (peek(l) == '-') { advance(l); t.type = TOKEN_DECREMENT; return t; }
    t.type = TOKEN_MINUS; return t;
  }

  if (c == '*') {
    advance(l);
    if (peek(l) == '=') { advance(l); t.type = TOKEN_STAR_ASSIGN; return t; }
    t.type = TOKEN_STAR; return t;
  }

  if (c == '/') {
    advance(l);
    if (peek(l) == '=') { advance(l); t.type = TOKEN_SLASH_ASSIGN; return t; }
    t.type = TOKEN_SLASH; return t;
  }

  if (c == '%') {
    advance(l);
    if (peek(l) == '=') { advance(l); t.type = TOKEN_MOD_ASSIGN; return t; }
    t.type = TOKEN_MOD; return t;
  }

  if (c == '&') {
    advance(l);
    if (peek(l) == '&') { advance(l); t.type = TOKEN_AND_AND; return t; }
    if (peek(l) == '=') { advance(l); t.type = TOKEN_AND_ASSIGN; return t; }
    t.type = TOKEN_AND; return t;
  }

  if (c == '|') {
    advance(l);
    if (peek(l) == '|') { advance(l); t.type = TOKEN_OR_OR; return t; }
    if (peek(l) == '=') { advance(l); t.type = TOKEN_OR_ASSIGN; return t; }
    t.type = TOKEN_OR; return t;
  }

  if (c == '^') {
    advance(l);
    if (peek(l) == '=') { advance(l); t.type = TOKEN_XOR_ASSIGN; return t; }
    t.type = TOKEN_XOR; return t;
  }

  if (c == '!') {
    advance(l);
    if (peek(l) == '=') { advance(l); t.type = TOKEN_NEQ; return t; }
    t.type = TOKEN_NOT; return t;
  }

  if (c == '<') {
    advance(l);
    if (peek(l) == '<') { 
        advance(l); 
        if (peek(l) == '=') { advance(l); t.type = TOKEN_LSHIFT_ASSIGN; return t; }
        t.type = TOKEN_LSHIFT; 
        return t; 
    }
    if (peek(l) == '=') { advance(l); t.type = TOKEN_LTE; return t; }
    t.type = TOKEN_LT; return t;
  }

  if (c == '>') {
    advance(l);
    if (peek(l) == '>') { 
        advance(l); 
        if (peek(l) == '=') { advance(l); t.type = TOKEN_RSHIFT_ASSIGN; return t; }
        t.type = TOKEN_RSHIFT; 
        return t; 
    }
    if (peek(l) == '=') { advance(l); t.type = TOKEN_GTE; return t; }
    t.type = TOKEN_GT; return t;
  }

  // --- Literals ---

  if (isdigit(c)) {
    long val = 0;
    while (isdigit(peek(l))) {
      val = val * 10 + (advance(l) - '0');
    }

    if (peek(l) == '.') {
      advance(l);
      double dval = (double)val;
      double fraction = 1.0;
      while (isdigit(peek(l))) {
        fraction /= 10.0;
        dval += (advance(l) - '0') * fraction;
      }
      t.type = TOKEN_FLOAT;
      t.double_val = dval;
      return t;
    }

    t.type = TOKEN_NUMBER;
    t.int_val = (int)val;
    return t;
  }

  if (c == '\'') {
      advance(l); 
      char val = advance(l);
      
      if (val == '\\') {
          char next = advance(l);
          switch(next) {
              case 'n': val = '\n'; break;
              case 't': val = '\t'; break;
              case 'r': val = '\r'; break;
              case '0': val = '\0'; break;
              case '\'': val = '\''; break;
              case '\\': val = '\\'; break;
              default: val = next; break; 
          }
      }
      
      if (peek(l) == '\'') advance(l); 
      else {
          fprintf(stderr, "Lexer Error: Unclosed character literal at %d:%d\n", l->line, l->col);
          exit(1);
      }
      
      t.type = TOKEN_CHAR_LIT;
      t.int_val = (int)val;
      return t;
  }

  if (c == '"') {
    advance(l); 
    
    size_t capacity = 32;
    size_t length = 0;
    char *buffer = malloc(capacity);
    if (!buffer) {
      fprintf(stderr, "Lexer Error: Out of memory\n");
      exit(1);
    }

    while (peek(l) != '"' && peek(l) != '\0') {
      char val = peek(l);
      if (val == '\\') {
        advance(l); 
        if (peek(l) == '\0') break; 

        char escaped = peek(l);
        switch (escaped) {
          case 'n': val = '\n'; break;
          case 'r': val = '\r'; break;
          case 't': val = '\t'; break;
          case '0': val = '\0'; break;
          case '\\': val = '\\'; break;
          case '"': val = '"'; break;
          case '\'': val = '\''; break;
          default: val = escaped; break; 
        }
        advance(l); 
      } else {
        advance(l); 
      }
      
      if (length + 1 >= capacity) {
        capacity *= 2;
        buffer = realloc(buffer, capacity);
        if (!buffer) {
          fprintf(stderr, "Lexer Error: Out of memory\n");
          exit(1);
        }
      }
      buffer[length++] = val;
    }
    
    buffer[length] = '\0';
    if (peek(l) == '"') advance(l);
    
    t.type = TOKEN_STRING;
    t.text = buffer;
    return t;
  }

  if (isalpha(c) || c == '_') {
    size_t capacity = 16;
    size_t length = 0;
    char *word = malloc(capacity);
    
    while (isalnum(peek(l)) || peek(l) == '_') {
        char wc = advance(l);
        if (length + 1 >= capacity) {
            capacity *= 2;
            word = realloc(word, capacity);
        }
        word[length++] = wc;
    }
    word[length] = '\0';

    if (strcmp(word, "loop") == 0) t.type = TOKEN_LOOP;
    else if (strcmp(word, "while") == 0) t.type = TOKEN_WHILE;
    else if (strcmp(word, "once") == 0) t.type = TOKEN_ONCE;
    else if (strcmp(word, "if") == 0) t.type = TOKEN_IF;
    else if (strcmp(word, "elif") == 0) t.type = TOKEN_ELIF;
    else if (strcmp(word, "else") == 0) t.type = TOKEN_ELSE;
    else if (strcmp(word, "return") == 0) t.type = TOKEN_RETURN;
    else if (strcmp(word, "break") == 0) t.type = TOKEN_BREAK;
    else if (strcmp(word, "continue") == 0) t.type = TOKEN_CONTINUE;
    
    else if (strcmp(word, "void") == 0) t.type = TOKEN_KW_VOID;
    else if (strcmp(word, "int") == 0) t.type = TOKEN_KW_INT;
    else if (strcmp(word, "char") == 0) t.type = TOKEN_KW_CHAR;
    else if (strcmp(word, "bool") == 0) t.type = TOKEN_KW_BOOL;
    else if (strcmp(word, "single") == 0) t.type = TOKEN_KW_SINGLE;
    else if (strcmp(word, "double") == 0) t.type = TOKEN_KW_DOUBLE;
    else if (strcmp(word, "let") == 0) t.type = TOKEN_KW_LET;
    
    else if (strcmp(word, "define") == 0) t.type = TOKEN_DEFINE;
    else if (strcmp(word, "as") == 0) t.type = TOKEN_AS;
    
    else if (strcmp(word, "mut") == 0) t.type = TOKEN_KW_MUT;
    else if (strcmp(word, "mutable") == 0) t.type = TOKEN_KW_MUT;
    else if (strcmp(word, "imut") == 0) t.type = TOKEN_KW_IMUT;
    else if (strcmp(word, "immutable") == 0) t.type = TOKEN_KW_IMUT;
    
    else if (strcmp(word, "import") == 0) t.type = TOKEN_IMPORT;
    else if (strcmp(word, "extern") == 0) t.type = TOKEN_EXTERN;
    else if (strcmp(word, "link") == 0) t.type = TOKEN_LINK;

    else if (strcmp(word, "true") == 0) t.type = TOKEN_TRUE;
    else if (strcmp(word, "false") == 0) t.type = TOKEN_FALSE;
    else if (strcmp(word, "not") == 0) t.type = TOKEN_NOT;
    else {
      t.type = TOKEN_IDENTIFIER;
      t.text = word;
      return t;
    }
    
    free(word);
    return t;
  }

  // Handle unexpected character by advancing to prevent infinite loops on stuck char
  advance(l);
  t.type = TOKEN_UNKNOWN;
  return t;
}

void free_token(Token t) {
  if (t.text) free(t.text);
}
