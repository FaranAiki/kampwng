#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

void lexer_init(Lexer *l, CompilerContext *ctx, const char *filename, const char* src) {
  l->src = src;
  l->filename = filename; 
  l->pos = 0;
  l->line = 1;
  l->col = 1;
  l->ctx = ctx;
}

static char peek(Lexer *l) { return l->src[l->pos]; }

static char advance(Lexer *l) {
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

void skip_whitespace_and_comments(Lexer *l) {
  while (1) {
    char c = peek(l);
    if (c == '\0') break;

    if (isspace(c)) {
      advance(l);
      continue;
    }

    // Single line comment
    if (c == '/' && l->src[l->pos + 1] == '/') {
      while (peek(l) != '\0' && peek(l) != '\n') {
        advance(l);
      }
      continue; 
    }

    // Block comment
    if (c == '/' && l->src[l->pos + 1] == '*') {
      advance(l); // consume '/'
      advance(l); // consume '*'
      
      while (1) {
          char next = peek(l);
          if (next == '\0') {
              // Can't report easily without token, just log
              if(l->ctx) l->ctx->error_count++;
              fprintf(stderr, "Unclosed block comment at line %d\n", l->line);
              return; 
          }
          
          if (next == '*' && l->src[l->pos + 1] == '/') {
              advance(l); // consume '*'
              advance(l); // consume '/'
              break;
          }
          
          advance(l);
      }
      continue;
    }

    break;
  }
}

static int lex_symbol(Lexer *l, Token *t) {
  char c = peek(l);

  if (c == '.') {
      if (l->src[l->pos+1] == '.' && l->src[l->pos+2] == '.') {
          advance(l); advance(l); advance(l);
          t->type = TOKEN_ELLIPSIS;
          return 1;
      }
      advance(l); t->type = TOKEN_DOT; return 1;
  }

  switch (c) {
      case ',': advance(l); t->type = TOKEN_COMMA; return 1;
      case ':': advance(l); t->type = TOKEN_COLON; return 1;
      case '[': advance(l); t->type = TOKEN_LBRACKET; return 1;
      case ']': advance(l); t->type = TOKEN_RBRACKET; return 1;
      case '{': advance(l); t->type = TOKEN_LBRACE; return 1;
      case '}': advance(l); t->type = TOKEN_RBRACE; return 1;
      case '(': advance(l); t->type = TOKEN_LPAREN; return 1;
      case ')': advance(l); t->type = TOKEN_RPAREN; return 1;
      case ';': advance(l); t->type = TOKEN_SEMICOLON; return 1;
      case '~': advance(l); t->type = TOKEN_BIT_NOT; return 1;
  }

  if (c == '=') { 
    advance(l); 
    if (peek(l) == '=') { advance(l); t->type = TOKEN_EQ; return 1; }
    t->type = TOKEN_ASSIGN; return 1; 
  }

  if (c == '+') {
    advance(l);
    if (peek(l) == '=') { advance(l); t->type = TOKEN_PLUS_ASSIGN; return 1; }
    if (peek(l) == '+') { advance(l); t->type = TOKEN_INCREMENT; return 1; }
    t->type = TOKEN_PLUS; return 1;
  }

  if (c == '-') {
    advance(l);
    if (peek(l) == '=') { advance(l); t->type = TOKEN_MINUS_ASSIGN; return 1; }
    if (peek(l) == '-') { advance(l); t->type = TOKEN_DECREMENT; return 1; }
    t->type = TOKEN_MINUS; return 1;
  }

  if (c == '*') {
    advance(l);
    if (peek(l) == '=') { advance(l); t->type = TOKEN_STAR_ASSIGN; return 1; }
    t->type = TOKEN_STAR; return 1;
  }

  if (c == '/') {
    advance(l);
    if (peek(l) == '=') { advance(l); t->type = TOKEN_SLASH_ASSIGN; return 1; }
    t->type = TOKEN_SLASH; return 1;
  }

  if (c == '%') {
    advance(l);
    if (peek(l) == '=') { advance(l); t->type = TOKEN_MOD_ASSIGN; return 1; }
    t->type = TOKEN_MOD; return 1;
  }

  if (c == '&') {
    advance(l);
    if (peek(l) == '&') { advance(l); t->type = TOKEN_AND_AND; return 1; }
    if (peek(l) == '=') { advance(l); t->type = TOKEN_AND_ASSIGN; return 1; }
    t->type = TOKEN_AND; return 1;
  }

  if (c == '|') {
    advance(l);
    if (peek(l) == '|') { advance(l); t->type = TOKEN_OR_OR; return 1; }
    if (peek(l) == '=') { advance(l); t->type = TOKEN_OR_ASSIGN; return 1; }
    t->type = TOKEN_OR; return 1;
  }

  if (c == '^') {
    advance(l);
    if (peek(l) == '=') { advance(l); t->type = TOKEN_XOR_ASSIGN; return 1; }
    t->type = TOKEN_XOR; return 1;
  }

  if (c == '!') {
    advance(l);
    if (peek(l) == '=') { advance(l); t->type = TOKEN_NEQ; return 1; }
    t->type = TOKEN_NOT; return 1;
  }

  if (c == '<') {
    advance(l);
    if (peek(l) == '<') { 
        advance(l); 
        if (peek(l) == '=') { advance(l); t->type = TOKEN_LSHIFT_ASSIGN; return 1; }
        t->type = TOKEN_LSHIFT; 
        return 1; 
    }
    if (peek(l) == '=') { advance(l); t->type = TOKEN_LTE; return 1; }
    t->type = TOKEN_LT; return 1;
  }

  if (c == '>') {
    advance(l);
    if (peek(l) == '>') { 
        advance(l); 
        if (peek(l) == '=') { advance(l); t->type = TOKEN_RSHIFT_ASSIGN; return 1; }
        t->type = TOKEN_RSHIFT; 
        return 1; 
    }
    if (peek(l) == '=') { advance(l); t->type = TOKEN_GTE; return 1; }
    t->type = TOKEN_GT; return 1;
  }

  return 0;
}

static int lex_number(Lexer *l, Token *t) {
    if (!isdigit(peek(l))) return 0;
    
    unsigned long long val = 0;
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
      
      t->type = TOKEN_FLOAT;
      t->double_val = dval;
      
      if (tolower(peek(l)) == 'l') {
          advance(l);
          t->type = TOKEN_LONG_DOUBLE_LIT;
      } else if (tolower(peek(l)) == 'f') {
          advance(l);
      }
      return 1;
    }

    int is_u = 0, is_l = 0, is_ll = 0;
    while (1) {
        char s = tolower(peek(l));
        if (s == 'u') { is_u = 1; advance(l); }
        else if (s == 'l') {
            advance(l);
            if (tolower(peek(l)) == 'l') {
                is_ll = 1; advance(l);
            } else {
                is_l = 1;
            }
        } else {
            break;
        }
    }

    t->type = TOKEN_NUMBER;
    if (is_ll && is_u) t->type = TOKEN_ULONG_LONG_LIT;
    else if (is_ll) t->type = TOKEN_LONG_LONG_LIT;
    else if (is_l && is_u) t->type = TOKEN_ULONG_LIT;
    else if (is_l) t->type = TOKEN_LONG_LIT;
    else if (is_u) t->type = TOKEN_UINT_LIT;
    
    t->double_val = (double)val; 
    t->int_val = (int)val;
    t->long_val = val;
    return 1;
}

static int lex_char(Lexer *l, Token *t) {
    if (peek(l) != '\'') return 0;
    
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
        // We can't exit(1) nicely here without setjmp, but for now we rely on parser to catch unknown tokens
        // Or check error recovery.
        if (l->ctx) l->ctx->error_count++;
        fprintf(stderr, "Lexer Error: Unclosed character literal at %d:%d\n", l->line, l->col);
    }
    
    t->type = TOKEN_CHAR_LIT;
    t->int_val = (int)val;
    t->long_val = (unsigned long long)val;
    return 1;
}

// Uses a temporary stack buffer to parse the string, then copies to Arena
static char* consume_string_content(Lexer *l) {
    char buffer[4096]; // Max literal size for now
    size_t length = 0;
    size_t cap = 4095;

    while (peek(l) != '"' && peek(l) != '\0') {
      if (length >= cap) break; // Truncate safety

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
      buffer[length++] = val;
    }
    buffer[length] = '\0';
    
    if (peek(l) == '"') advance(l);
    
    // Allocate final string in arena
    return arena_strndup(l->ctx->arena, buffer, length);
}

static int lex_string(Lexer *l, Token *t) {
  char c = peek(l);
  
  // C-String check: c"..."
  if (c == 'c' && l->src[l->pos + 1] == '"') {
    advance(l); // consume 'c'
    advance(l); // consume '"'
    
    t->type = TOKEN_C_STRING;
    t->text = consume_string_content(l);
    return 1;
  }

  if (c == '"') {
    advance(l); 
    t->type = TOKEN_STRING;
    t->text = consume_string_content(l);
    return 1;
  }
  
  return 0;
}

static int lex_word(Lexer *l, Token *t) {
  char c = peek(l);
  if (!isalpha(c) && c != '_') return 0;
  
  const char *start = &l->src[l->pos];
  size_t length = 0;

  while (isalnum(peek(l)) || peek(l) == '_') {
      advance(l);
      length++;
  }
  
  // Check keywords using a temporary stack buffer for null-termination check
  // or simple comparison if we had a length-based keyword checker.
  // For now, duplicate to arena to perform string checks (slightly inefficient but clean with Arena)
  // OR use a local stack buffer for comparison.
  
  char word[256];
  if (length < 256) {
      strncpy(word, start, length);
      word[length] = '\0';
  } else {
      // Too long for a keyword, treating as identifier
      t->type = TOKEN_IDENTIFIER;
      t->text = arena_strndup(l->ctx->arena, start, length);
      return 1;
  }

  // Keyword Checks
  if (strcmp(word, "loop") == 0) t->type = TOKEN_LOOP;
  else if (strcmp(word, "while") == 0) t->type = TOKEN_WHILE;
  else if (strcmp(word, "once") == 0) t->type = TOKEN_ONCE;
  else if (strcmp(word, "if") == 0) t->type = TOKEN_IF;
  else if (strcmp(word, "elif") == 0) t->type = TOKEN_ELIF;
  else if (strcmp(word, "then") == 0) t->type = TOKEN_THEN;
  else if (strcmp(word, "else") == 0) t->type = TOKEN_ELSE;
  else if (strcmp(word, "return") == 0) t->type = TOKEN_RETURN;
  else if (strcmp(word, "break") == 0) t->type = TOKEN_BREAK;
  else if (strcmp(word, "continue") == 0) t->type = TOKEN_CONTINUE;
  
  else if (strcmp(word, "switch") == 0) t->type = TOKEN_SWITCH; 
  else if (strcmp(word, "case") == 0) t->type = TOKEN_CASE;     
  else if (strcmp(word, "default") == 0) t->type = TOKEN_DEFAULT; 
  else if (strcmp(word, "leak") == 0) t->type = TOKEN_LEAK;     

  else if (strcmp(word, "define") == 0) t->type = TOKEN_DEFINE;
  else if (strcmp(word, "as") == 0) t->type = TOKEN_AS;
  else if (strcmp(word, "typedef") == 0) t->type = TOKEN_TYPEDEF;
  
  else if (strcmp(word, "class") == 0) t->type = TOKEN_CLASS;
  else if (strcmp(word, "struct") == 0) t->type = TOKEN_STRUCT;
  else if (strcmp(word, "union") == 0) t->type = TOKEN_UNION;
  else if (strcmp(word, "is") == 0) t->type = TOKEN_IS;
  else if (strcmp(word, "has") == 0) t->type = TOKEN_HAS;
  else if (strcmp(word, "open") == 0) t->type = TOKEN_OPEN;
  else if (strcmp(word, "closed") == 0) t->type = TOKEN_CLOSED;
  else if (strcmp(word, "typeof") == 0) t->type = TOKEN_TYPEOF;
  else if (strcmp(word, "hasmethod") == 0) t->type = TOKEN_HASMETHOD;
  else if (strcmp(word, "hasattribute") == 0) t->type = TOKEN_HASATTRIBUTE;
  else if (strcmp(word, "namespace") == 0) t->type = TOKEN_NAMESPACE;
  else if (strcmp(word, "enum") == 0) t->type = TOKEN_ENUM; 

  else if (strcmp(word, "flux") == 0) t->type = TOKEN_FLUX;
  else if (strcmp(word, "emit") == 0) t->type = TOKEN_EMIT;
  else if (strcmp(word, "for") == 0) t->type = TOKEN_FOR;
  else if (strcmp(word, "in") == 0) t->type = TOKEN_IN;

  else if (strcmp(word, "void") == 0) t->type = TOKEN_KW_VOID;
  else if (strcmp(word, "int") == 0) t->type = TOKEN_KW_INT;
  else if (strcmp(word, "short") == 0) t->type = TOKEN_KW_SHORT;
  else if (strcmp(word, "long") == 0) t->type = TOKEN_KW_LONG;
  else if (strcmp(word, "unsigned") == 0) t->type = TOKEN_KW_UNSIGNED;
  else if (strcmp(word, "char") == 0) t->type = TOKEN_KW_CHAR;
  else if (strcmp(word, "bool") == 0) t->type = TOKEN_KW_BOOL;
  else if (strcmp(word, "single") == 0) t->type = TOKEN_KW_SINGLE;
  else if (strcmp(word, "double") == 0) t->type = TOKEN_KW_DOUBLE;
  else if (strcmp(word, "string") == 0) t->type = TOKEN_KW_STRING;
  else if (strcmp(word, "let") == 0) t->type = TOKEN_KW_LET;
  
  else if (strcmp(word, "mut") == 0) t->type = TOKEN_KW_MUT;
  else if (strcmp(word, "mutable") == 0) t->type = TOKEN_KW_MUT;
  else if (strcmp(word, "imut") == 0) t->type = TOKEN_KW_IMUT;
  else if (strcmp(word, "immutable") == 0) t->type = TOKEN_KW_IMUT;
  
  else if (strcmp(word, "import") == 0) t->type = TOKEN_IMPORT;
  else if (strcmp(word, "extern") == 0) t->type = TOKEN_EXTERN;
  else if (strcmp(word, "link") == 0) t->type = TOKEN_LINK;

  else if (strcmp(word, "true") == 0) t->type = TOKEN_TRUE;
  else if (strcmp(word, "false") == 0) t->type = TOKEN_FALSE;
  else if (strcmp(word, "not") == 0) t->type = TOKEN_NOT;
  else {
    t->type = TOKEN_IDENTIFIER;
    t->text = arena_strndup(l->ctx->arena, start, length);
    return 1;
  }
  
  return 1;
}

Token lexer_next(Lexer *l) {
  skip_whitespace_and_comments(l);

  Token t = {TOKEN_UNKNOWN, NULL, 0, 0, 0.0, l->line, l->col};
  char c = peek(l);

  if (c == '\0') {
    t.type = TOKEN_EOF;
    return t;
  }

  if (lex_symbol(l, &t)) return t;
  if (lex_number(l, &t)) return t;
  if (lex_char(l, &t)) return t;
  if (lex_string(l, &t)) return t;
  if (lex_word(l, &t)) return t;

  advance(l);
  t.type = TOKEN_UNKNOWN;
  return t;
}
