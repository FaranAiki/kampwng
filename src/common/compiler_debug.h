// How to make C do this huhh

#ifndef COMPILER_DEBUG_H
#define COMPILER_DEBUG_H

#define DIAG_RED    "\033[1;31m"
#define DIAG_GREEN  "\033[1;32m"
#define DIAG_YELLOW "\033[1;33m"
#define DIAG_BLUE   "\033[1;34m"
#define DIAG_PURPLE "\033[1;35m"
#define DIAG_CYAN   "\033[1;36m"
#define DIAG_GREY   "\033[0;90m"
#define DIAG_BOLD   "\033[1m"
#define DIAG_RESET  "\033[0m"

#define DEBUG_FLOW
#ifdef DEBUG_FLOW 
  #define debug_flow(msg, ...) printf(__FILE__ ": " DIAG_BLUE "flow: " DIAG_RESET msg, ##__VA_ARGS__); putchar('\n');
#else 
  #define debug_flow(msg, ...) 
#endif // DEBUG_FLOW 

#define DEBUG_STEP
#ifdef DEBUG_STEP 
  #define debug_step(msg, ...) printf(DIAG_CYAN "step: " DIAG_RESET msg, ##__VA_ARGS__); putchar('\n');
#else 
  #define debug_step(msg, ...) 
#endif // DEBUG_STEP

#define DEBUG_LEXER_OUT
#ifdef DEBUG_LEXER_OUT 
  #define to_token_out(l, f) lexer_to_file(l, f)
#else 
  #define to_token_out(l, f)
#endif // DEBUG_TOKEN_OUT 

#define DEBUG_PARSER_OUT
#ifdef DEBUG_PARSER_OUT 
  #define to_ast_out(p, f) parser_to_file(p, f)
#else 
  #define to_ast_out(p, f) 
#endif // DEBUG_PARSER_OUT

#define DEBUG_SEMANTIC_OUT
#ifdef DEBUG_SEMANTIC_OUT 
  #define to_sem_out(p, f) semantic_to_file(p, f)
#else 
  #define to_sem_out(p, f) 
#endif // DEBUG_PARSER_OUT

#endif // COMPILER_DEBUG_H
