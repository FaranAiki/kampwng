#include "codegen.h"

char* format_string(const char* input) {
  if (!input) return NULL;
  size_t len = strlen(input);
  char *new_str = malloc(len + 1);
  strcpy(new_str, input);
  return new_str;
}
