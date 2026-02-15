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
  #define debug_flow(msg, ...) printf(DIAG_BLUE "flow: " DIAG_RESET msg, ##__VA_ARGS__); putchar('\n');
#else 
  #define debug_flow(msg, ...) 
#endif

#define DEBUG_STEP 
#ifdef DEBUG_STEP 
  #define debug_step(msg, ...) printf(DIAG_CYAN "step: " DIAG_RESET msg, ##__VA_ARGS__); putchar('\n');
#else 
  #define debug_step(msg, ...) 
#endif

#endif // COMPILER_DEBUG_H
