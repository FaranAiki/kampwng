#include "parser_internal.h"
#include <string.h>

// Global State
Token current_token = {TOKEN_UNKNOWN, NULL, 0, 0.0};
jmp_buf *parser_env = NULL;

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

VarType get_type_from_token(TokenType t) {
  switch(t) {
    case TOKEN_KW_INT: return VAR_INT;
    case TOKEN_KW_CHAR: return VAR_CHAR;
    case TOKEN_KW_BOOL: return VAR_BOOL;
    case TOKEN_KW_SINGLE: return VAR_FLOAT;
    case TOKEN_KW_DOUBLE: return VAR_DOUBLE;
    case TOKEN_KW_VOID: return VAR_VOID;
    case TOKEN_KW_LET: return VAR_AUTO;
    default: return -1;
  }
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
