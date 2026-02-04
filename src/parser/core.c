#include "parser_internal.h"
#include <string.h>

// Global State
Token current_token = {TOKEN_UNKNOWN, NULL, 0, 0.0};
jmp_buf *parser_env = NULL;

// Type Aliases Linked List
typedef struct TypeAlias {
  char *name;
  VarType type;
  struct TypeAlias *next;
} TypeAlias;

TypeAlias *alias_head = NULL;

void register_alias(const char *name, VarType type) {
  TypeAlias *a = malloc(sizeof(TypeAlias));
  a->name = strdup(name);
  a->type = type;
  a->next = alias_head;
  alias_head = a;
}

VarType find_alias(const char *name) {
  TypeAlias *curr = alias_head;
  while(curr) {
    if (strcmp(curr->name, name) == 0) return curr->type;
    curr = curr->next;
  }
  VarType t = {TYPE_UNKNOWN, 0};
  return t;
}

void parser_set_recovery(jmp_buf *env) {
    parser_env = env;
}

void safe_free_current_token() {
  if (current_token.text) {
    free(current_token.text);
    current_token.text = NULL;
  }
}

void parser_fail(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    safe_free_current_token();
    if (parser_env) {
        longjmp(*parser_env, 1);
    } else {
        exit(1);
    }
}

void parser_reset(void) {
    safe_free_current_token();
    current_token.type = TOKEN_UNKNOWN;
    current_token.int_val = 0;
    current_token.double_val = 0.0;
    current_token.line = 0;
    current_token.col = 0;
    parser_env = NULL;
    // Note: We do NOT clear aliases here to persist them in REPL
}

void eat(Lexer *l, TokenType type) {
  if (current_token.type == type) {
    safe_free_current_token();
    current_token = lexer_next(l);
  } else {
    char msg[100];
    sprintf(msg, "Parser Error: Unexpected token %d, expected %d at line %d:%d", 
            current_token.type, type, current_token.line, current_token.col);
    parser_fail(msg);
  }
}

// Parses type, including pointers (int*) and aliases
VarType parse_type(Lexer *l) {
  VarType t = {TYPE_UNKNOWN, 0};
  
  if (current_token.type == TOKEN_IDENTIFIER) {
    t = find_alias(current_token.text);
    if (t.base == TYPE_UNKNOWN) {
      // Not an alias
      return t;
    }
    eat(l, TOKEN_IDENTIFIER);
  } else {
    switch(current_token.type) {
      case TOKEN_KW_INT: t.base = TYPE_INT; break;
      case TOKEN_KW_CHAR: t.base = TYPE_CHAR; break;
      case TOKEN_KW_BOOL: t.base = TYPE_BOOL; break;
      case TOKEN_KW_SINGLE: t.base = TYPE_FLOAT; break;
      case TOKEN_KW_DOUBLE: t.base = TYPE_DOUBLE; break;
      case TOKEN_KW_VOID: t.base = TYPE_VOID; break;
      case TOKEN_KW_LET: t.base = TYPE_AUTO; break;
      default: return t; // Unknown
    }
    eat(l, current_token.type);
  }

  // Handle Pointers (int*, int**)
  while (current_token.type == TOKEN_STAR) {
    t.ptr_depth++;
    eat(l, TOKEN_STAR);
  }
  
  return t;
}

char* read_import_file(const char* filename) {
  FILE* f = fopen(filename, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* buf = malloc(len + 1);
  fread(buf, 1, len, f);
  buf[len] = '\0';
  fclose(f);
  return buf;
}

void free_ast(ASTNode *node) {
  if (!node) return;
  if (node->next) free_ast(node->next);
  
  if (node->type == NODE_FUNC_DEF) {
    FuncDefNode *f = (FuncDefNode*)node;
    free(f->name);
    free_ast(f->body);
  }
  else if (node->type == NODE_VAR_DECL) {
    free(((VarDeclNode*)node)->name);
    free_ast(((VarDeclNode*)node)->initializer);
    free_ast(((VarDeclNode*)node)->array_size);
  } else if (node->type == NODE_ASSIGN) {
    free(((AssignNode*)node)->name);
    free_ast(((AssignNode*)node)->value);
    free_ast(((AssignNode*)node)->index);
  } else if (node->type == NODE_VAR_REF) {
    free(((VarRefNode*)node)->name);
  } else if (node->type == NODE_ARRAY_ACCESS) {
    free(((ArrayAccessNode*)node)->name);
    free_ast(((ArrayAccessNode*)node)->index);
  } else if (node->type == NODE_ARRAY_LIT) {
    free_ast(((ArrayLitNode*)node)->elements);
  } else if (node->type == NODE_LINK) {
    free(((LinkNode*)node)->lib_name);
  }
  free(node);
}
