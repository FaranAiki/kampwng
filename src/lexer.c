#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

void lexer_init(Lexer *l, const char *src) {
    l->src = src;
    l->pos = 0;
}

char peek(Lexer *l) { return l->src[l->pos]; }
char advance(Lexer *l) { return l->src[l->pos++]; }

Token lexer_next(Lexer *l) {
    Token t = {TOKEN_UNKNOWN, NULL, 0, 0.0};
    
    // Skip whitespace and comments
    while (1) {
        char c = peek(l);
        if (c == '\0') break;

        // Skip whitespace
        if (isspace(c)) {
            advance(l);
            continue;
        }

        // Skip comments (// ...)
        if (c == '/' && l->src[l->pos + 1] == '/') {
            while (peek(l) != '\0' && peek(l) != '\n') {
                advance(l);
            }
            continue; // Continue loop to skip trailing whitespace/newlines
        }

        break;
    }

    char c = peek(l);

    if (c == '\0') {
        t.type = TOKEN_EOF;
        return t;
    }

    if (c == ',') { advance(l); t.type = TOKEN_COMMA; return t; }

    // Two-character operators
    if (c == '=') { 
        advance(l); 
        if (peek(l) == '=') { advance(l); t.type = TOKEN_EQ; return t; }
        t.type = TOKEN_ASSIGN; return t; 
    }
    if (c == '!') {
        advance(l);
        if (peek(l) == '=') { advance(l); t.type = TOKEN_NEQ; return t; }
        t.type = TOKEN_NOT; return t;
    }
    if (c == '<') {
        advance(l);
        if (peek(l) == '<') { advance(l); t.type = TOKEN_LSHIFT; return t; }
        if (peek(l) == '=') { advance(l); t.type = TOKEN_LTE; return t; }
        t.type = TOKEN_LT; return t;
    }
    if (c == '>') {
        advance(l);
        if (peek(l) == '>') { advance(l); t.type = TOKEN_RSHIFT; return t; }
        if (peek(l) == '=') { advance(l); t.type = TOKEN_GTE; return t; }
        t.type = TOKEN_GT; return t;
    }

    // Single Character Tokens
    if (c == '[') { advance(l); t.type = TOKEN_LBRACKET; return t; }
    if (c == ']') { advance(l); t.type = TOKEN_RBRACKET; return t; }
    if (c == '{') { advance(l); t.type = TOKEN_LBRACE; return t; }
    if (c == '}') { advance(l); t.type = TOKEN_RBRACE; return t; }
    if (c == '(') { advance(l); t.type = TOKEN_LPAREN; return t; }
    if (c == ')') { advance(l); t.type = TOKEN_RPAREN; return t; }
    if (c == ';') { advance(l); t.type = TOKEN_SEMICOLON; return t; }
    if (c == '+') { advance(l); t.type = TOKEN_PLUS; return t; }
    if (c == '-') { advance(l); t.type = TOKEN_MINUS; return t; }
    if (c == '*') { advance(l); t.type = TOKEN_STAR; return t; }
    if (c == '/') { advance(l); t.type = TOKEN_SLASH; return t; }
    if (c == '^') { advance(l); t.type = TOKEN_XOR; return t; }

    // Numbers
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

    // Strings
    if (c == '"') {
        advance(l); 
        int start = l->pos;
        while (peek(l) != '"' && peek(l) != '\0') {
            advance(l);
        }
        
        int len = l->pos - start;
        t.text = malloc(len + 1);
        if (t.text) {
            strncpy(t.text, l->src + start, len);
            t.text[len] = '\0';
        }
        
        if (peek(l) == '"') advance(l);
        
        t.type = TOKEN_STRING;
        return t;
    }

    // Keywords and Identifiers
    if (isalpha(c)) {
        int start = l->pos;
        while (isalnum(peek(l)) || peek(l) == '_') advance(l);
        int len = l->pos - start;
        char *word = malloc(len + 1);
        strncpy(word, l->src + start, len);
        word[len] = '\0';

        // Keywords
        if (strcmp(word, "loop") == 0) t.type = TOKEN_LOOP;
        else if (strcmp(word, "if") == 0) t.type = TOKEN_IF;
        else if (strcmp(word, "elif") == 0) t.type = TOKEN_ELIF;
        else if (strcmp(word, "else") == 0) t.type = TOKEN_ELSE;
        else if (strcmp(word, "return") == 0) t.type = TOKEN_RETURN;
        else if (strcmp(word, "void") == 0) t.type = TOKEN_KW_VOID;
        else if (strcmp(word, "int") == 0) t.type = TOKEN_KW_INT;
        else if (strcmp(word, "char") == 0) t.type = TOKEN_KW_CHAR;
        else if (strcmp(word, "bool") == 0) t.type = TOKEN_KW_BOOL;
        else if (strcmp(word, "single") == 0) t.type = TOKEN_KW_SINGLE;
        else if (strcmp(word, "double") == 0) t.type = TOKEN_KW_DOUBLE;
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

    advance(l);
    return t;
}

void free_token(Token t) {
    if (t.text) {
        free(t.text);
    }
}
