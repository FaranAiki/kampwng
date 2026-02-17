#ifndef DIAGNOSTIC_H
#define DIAGNOSTIC_H

#include "../lexer/lexer.h"

// Context Tracking
void diag_set_namespace(const char *ns);
const char* diag_get_namespace(void);

// Report a detailed error with source snippet
void report_error(Lexer *l, Token t, const char *msg);

// Report a warning (Purple/Magenta)
void report_warning(Lexer *l, Token t, const char *msg);

// Report a hint (Yellow)
// Standardized "Did you mean 'X'?" or general hints
void report_hint(Lexer *l, Token t, const char *msg);

// Report info (Blue)
void report_info(Lexer *l, Token t, const char *msg);

// Report a reason (Purple)
// Used to provide context (e.g., "previous definition was here")
void report_reason(Lexer *l, Token t, const char *msg);

#include "../parser/parser.h"

// Convert a token type to a human-readable string (e.g., TOKEN_SEMICOLON -> ";")
const char* token_type_to_string(TokenType type);
// const char* node_type_to_string(NodeType type);

// Helper to hint about missing delimiters
const char* get_token_description(TokenType type);

// "Did you mean?" logic
const char* find_closest_keyword(const char *ident);

// Expose distance calc for codegen usage
int levenshtein_dist(const char *s1, const char *s2);

#endif
