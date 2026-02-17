#ifndef CONTEXT_H
#define CONTEXT_H

#include "arena.h"
#include <setjmp.h>
#include <stddef.h>

// Holds the global state for a single compilation session
typedef struct {
  Arena *arena;
    
  int lexer_error_count;
  int parser_error_count;
  int semantic_error_count;
  int alir_error_count;

  jmp_buf recover_buf;
  int has_recovery;    // Flag to ensure jump buffer is valid
  int error_count;
    
  // Diagnostic State (formerly globals in diagnostic.c)
  char current_namespace[256];
  char last_reported_namespace[256];
  char last_reported_filename[1024];

} CompilerContext;

// Initialize the context with a provided arena
void context_init(CompilerContext *ctx, Arena *arena);

#endif // CONTEXT_H
