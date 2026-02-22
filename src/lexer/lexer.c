#include "lexer.h"
#include "common.h"
#include "common/diagnostic.h"
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

static char* intern_string(Lexer *l, const char *str) {
    if (!str) return NULL;
    
    void *existing = hashmap_get(&l->ctx->string_pool, str);
    if (existing) {
        return (char*)existing; // Reuse the pointer!
    }
    
    char *new_str = arena_strdup(l->ctx->arena, str);
    
    // Both key and value are the same pointer, saving space.
    hashmap_put(&l->ctx->string_pool, new_str, new_str);
    return new_str;
}

static char* intern_strndup(Lexer *l, const char *str, size_t len) {
    char *temp = malloc(len + 1);
    if (!temp) return NULL;
    
    memcpy(temp, str, len);
    temp[len] = '\0';
    char* res = intern_string(l, temp);
    
    free(temp);
    return res;
}

void skip_whitespace_and_comments(Lexer *l) {
  while (1) {
    char c = peek(l);
    if (c == '\0') break;

    if (isspace((unsigned char)c)) {
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
              Token dummy = {TOKEN_UNKNOWN, NULL, 0, 0, 0.0, l->line, l->col};
              report_error(l, dummy, "Unclosed block comment");
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
    
    const char *start = &l->src[l->pos];
    int length = 0;
    unsigned long long val = 0;
    
    while (isdigit(peek(l))) {
      val = val * 10 + (peek(l) - '0');
      advance(l);
      length++;
    }

    if (peek(l) == '.') {
      advance(l);
      length++;
      
      while (isdigit(peek(l))) {
        advance(l);
        length++;
      }
      
      char *buf = malloc(length + 1);
      memcpy(buf, start, length);
      buf[length] = '\0';
      double dval = strtod(buf, NULL);
      free(buf);
      
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
    
    if (peek(l) == '\'') {
        advance(l); 
    } else {
        Token dummy = {TOKEN_UNKNOWN, NULL, 0, 0, 0.0, l->line, l->col};
        report_error(l, dummy, "Unclosed character literal");
    }
    
    t->type = TOKEN_CHAR_LIT;
    t->int_val = (int)val;
    t->long_val = (unsigned long long)val;
    return 1;
}

// Dynamically scales using StringBuilder, eliminating hard limits and data loss.
static char* consume_string_content(Lexer *l) {
    StringBuilder sb;
    sb_init(&sb, NULL);

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
      sb_append_c(&sb, val);
    }
    
    if (peek(l) == '"') advance(l);
    
    // Check pool/allocate into pool safely 
    char *final_str = intern_string(l, sb.data ? sb.data : "");
    sb_free(&sb);
    return final_str;
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

// O(log N) Keyword Definitions mapping 
// CRITICAL: Array MUST remain sorted alphabetically for bsearch
static const int num_keywords = sizeof(keywords) / sizeof(keywords[0]);

static int compare_keywords(const void *a, const void *b) {
    return strcmp((const char *)a, ((const KeywordDef *)b)->word);
}

static int is_ident_start(char c) {
    unsigned char uc = (unsigned char)c;
    return isalpha(uc) || uc == '_' || uc >= 0x80;
}

static int is_ident_part(char c) {
    unsigned char uc = (unsigned char)c;
    return isalnum(uc) || uc == '_' || uc >= 0x80;
}

static int lex_word(Lexer *l, Token *t) {
  char c = peek(l);
  if (!is_ident_start(c)) return 0;
  
  const char *start = &l->src[l->pos];
  size_t length = 0;

  while (is_ident_part(peek(l))) {
      advance(l);
      length++;
  }
  
  char word[256];
  if (length < 256) {
      strncpy(word, start, length);
      word[length] = '\0';
      
      // O(log N) Keyword Check
      const KeywordDef *found = bsearch(word, keywords, num_keywords, sizeof(KeywordDef), compare_keywords);
      if (found) {
          t->type = found->type;
          return 1;
      }
  }

  // Fallback to identifier for unknown or extremely long strings
  t->type = TOKEN_IDENTIFIER;
  t->text = intern_strndup(l, start, length);
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
