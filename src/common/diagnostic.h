#ifndef DIAGNOSTIC_H
#define DIAGNOSTIC_H

#include "../lexer/lexer.h"
#include "context.h"

// Context Tracking
void diag_set_namespace(CompilerContext *ctx, const char *ns);
const char* diag_get_namespace(CompilerContext *ctx);

// Report a detailed error with source snippet
// Note: Lexer contains the pointer to Context
void report_error(Lexer *l, Token t, const char *msg);

// Report a warning (Purple/Magenta)
void report_warning(Lexer *l, Token t, const char *msg);

// Report a hint (Yellow)
void report_hint(Lexer *l, Token t, const char *msg);

// Report info (Blue)
void report_info(Lexer *l, Token t, const char *msg);

// Report a reason (Purple)
void report_reason(Lexer *l, Token t, const char *msg);

#include "../parser/parser.h"

const char* token_type_to_string(TokenType type);
const char* get_token_description(TokenType type);
const char* find_closest_keyword(const char *ident);
int levenshtein_dist(const char *s1, const char *s2);

#endif
