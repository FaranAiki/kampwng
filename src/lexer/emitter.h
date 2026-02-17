#ifndef LEXER_EMITTER_H
#define LEXER_EMITTER_H

#include "lexer.h"
#include "../common/common.h"

// Consumes the lexer and returns a string representation of all tokens
char* lexer_to_string(Lexer *l);

// Consumes the lexer and writes the string representation to a file
void lexer_to_file(Lexer *l, const char *filename);

// Helper: Initializes a temporary lexer with src, converts to string, cleans up
char* lexer_string_to_string(const char *src);

// Helper: Initializes a temporary lexer with src, writes to file, cleans up
void lexer_string_to_file(const char *src, const char *filename);

#include "../common/diagnostic.h"

#endif // LEXER_EMITTER_H
