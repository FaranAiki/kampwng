#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Token current_token = {TOKEN_UNKNOWN, NULL, 0, 0.0};
jmp_buf *parser_env = NULL;

void parser_set_recovery(jmp_buf *env) {
    parser_env = env;
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

void safe_free_current_token() {
  if (current_token.text) {
    free(current_token.text);
    current_token.text = NULL;
  }
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

// --- FORWARD DECLARATIONS ---
ASTNode* parse_loop(Lexer *l);
ASTNode* parse_if(Lexer *l);
ASTNode* parse_assignment_or_call(Lexer *l);
ASTNode* parse_var_decl_internal(Lexer *l);
ASTNode* parse_return(Lexer *l);
ASTNode* parse_statements(Lexer *l);
ASTNode* parse_factor(Lexer *l);
ASTNode* parse_single_statement_or_block(Lexer *l);

VarType get_type_from_token(TokenType t);

// --- HELPER FOR IMPORT ---
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

// --- EXPRESSION PARSER ---

ASTNode* parse_expression(Lexer *l);

ASTNode* parse_call(Lexer *l, char *name) {
  eat(l, TOKEN_LPAREN);
  
  ASTNode *args_head = NULL;
  ASTNode **curr_arg = &args_head;
  
  if (current_token.type != TOKEN_RPAREN) {
    *curr_arg = parse_expression(l);
    curr_arg = &(*curr_arg)->next;
    
    while (current_token.type == TOKEN_COMMA) {
      eat(l, TOKEN_COMMA);
      *curr_arg = parse_expression(l);
      curr_arg = &(*curr_arg)->next;
    }
  }
  
  eat(l, TOKEN_RPAREN);
  
  CallNode *node = calloc(1, sizeof(CallNode));
  node->base.type = NODE_CALL;
  node->name = name;
  node->args = args_head;
  return (ASTNode*)node;
}

ASTNode* parse_factor(Lexer *l) {
  if (current_token.type == TOKEN_LBRACKET) {
    // Array Literal: [1, 2, 3]
    eat(l, TOKEN_LBRACKET);
    
    ASTNode *elems_head = NULL;
    ASTNode **curr_elem = &elems_head;

    if (current_token.type != TOKEN_RBRACKET) {
      *curr_elem = parse_expression(l);
      curr_elem = &(*curr_elem)->next;
      
      while (current_token.type == TOKEN_COMMA) {
        eat(l, TOKEN_COMMA);
        *curr_elem = parse_expression(l);
        curr_elem = &(*curr_elem)->next;
      }
    }
    eat(l, TOKEN_RBRACKET);
    
    ArrayLitNode *node = calloc(1, sizeof(ArrayLitNode));
    node->base.type = NODE_ARRAY_LIT;
    node->elements = elems_head;
    return (ASTNode*)node;
  }
  else if (current_token.type == TOKEN_NUMBER) {
    LiteralNode *node = calloc(1, sizeof(LiteralNode));
    node->base.type = NODE_LITERAL;
    node->var_type = VAR_INT;
    node->val.int_val = current_token.int_val;
    eat(l, TOKEN_NUMBER);
    return (ASTNode*)node;
  }
  else if (current_token.type == TOKEN_CHAR_LIT) {
    LiteralNode *node = calloc(1, sizeof(LiteralNode));
    node->base.type = NODE_LITERAL;
    node->var_type = VAR_CHAR;
    node->val.int_val = current_token.int_val;
    eat(l, TOKEN_CHAR_LIT);
    return (ASTNode*)node;
  }
  else if (current_token.type == TOKEN_FLOAT) {
    LiteralNode *node = calloc(1, sizeof(LiteralNode));
    node->base.type = NODE_LITERAL;
    node->var_type = VAR_DOUBLE;
    node->val.double_val = current_token.double_val;
    eat(l, TOKEN_FLOAT);
    return (ASTNode*)node;
  }
  else if (current_token.type == TOKEN_STRING) {
    LiteralNode *node = calloc(1, sizeof(LiteralNode));
    node->base.type = NODE_LITERAL;
    node->var_type = VAR_STRING;
    node->val.str_val = current_token.text;
    current_token.text = NULL; 
    eat(l, TOKEN_STRING);
    return (ASTNode*)node;
  }
  else if (current_token.type == TOKEN_TRUE || current_token.type == TOKEN_FALSE) {
    LiteralNode *node = calloc(1, sizeof(LiteralNode));
    node->base.type = NODE_LITERAL;
    node->var_type = VAR_BOOL;
    node->val.int_val = (current_token.type == TOKEN_TRUE) ? 1 : 0;
    eat(l, current_token.type);
    return (ASTNode*)node;
  }
  else if (current_token.type == TOKEN_IDENTIFIER) {
    char *name = current_token.text;
    current_token.text = NULL;
    eat(l, TOKEN_IDENTIFIER);
    
    if (current_token.type == TOKEN_LPAREN) {
      return parse_call(l, name);
    }
    
    if (current_token.type == TOKEN_LBRACKET) {
      eat(l, TOKEN_LBRACKET);
      ASTNode *index = parse_expression(l);
      eat(l, TOKEN_RBRACKET);
      
      ArrayAccessNode *node = calloc(1, sizeof(ArrayAccessNode));
      node->base.type = NODE_ARRAY_ACCESS;
      node->name = name;
      node->index = index;
      return (ASTNode*)node;
    }
    
    // Command Style Call Detection (Expression Level)
    // Avoids operators to prevent ambiguous parsing like `x + y` -> `x(+y)`
    TokenType t = current_token.type;
    int is_arg_start = (t == TOKEN_NUMBER || t == TOKEN_FLOAT || t == TOKEN_STRING || 
          t == TOKEN_CHAR_LIT || t == TOKEN_TRUE || t == TOKEN_FALSE || 
          t == TOKEN_IDENTIFIER || t == TOKEN_LPAREN || t == TOKEN_LBRACKET || 
          t == TOKEN_NOT);

    if (is_arg_start) {
        ASTNode *args_head = NULL;
        ASTNode **curr_arg = &args_head;
        
        *curr_arg = parse_expression(l);
        curr_arg = &(*curr_arg)->next;

        while (current_token.type == TOKEN_COMMA) {
            eat(l, TOKEN_COMMA);
            *curr_arg = parse_expression(l);
            curr_arg = &(*curr_arg)->next;
        }

        CallNode *node = calloc(1, sizeof(CallNode));
        node->base.type = NODE_CALL;
        node->name = name;
        node->args = args_head;
        return (ASTNode*)node;
    }
    
    VarRefNode *node = calloc(1, sizeof(VarRefNode));
    node->base.type = NODE_VAR_REF;
    node->name = name;
    return (ASTNode*)node;
  }
  else if (current_token.type == TOKEN_LPAREN) {
    eat(l, TOKEN_LPAREN);
    ASTNode *expr = parse_expression(l);
    eat(l, TOKEN_RPAREN);
    return expr;
  } 
  else {
    char msg[100];
    sprintf(msg, "Parser Error: Unexpected token in expression: %d at line %d:%d", 
            current_token.type, current_token.line, current_token.col);
    parser_fail(msg);
    return NULL; // Unreachable
  }
}

ASTNode* parse_unary(Lexer *l) {
  if (current_token.type == TOKEN_NOT || current_token.type == TOKEN_MINUS) {
    int op = current_token.type;
    eat(l, op);
    ASTNode *operand = parse_unary(l);
    
    UnaryOpNode *node = calloc(1, sizeof(UnaryOpNode));
    node->base.type = NODE_UNARY_OP;
    node->op = op;
    node->operand = operand;
    return (ASTNode*)node;
  }
  return parse_factor(l);
}

// ... Binary Op Parsers ...
ASTNode* parse_binary_op(Lexer *l, ASTNode* (*sub_parser)(Lexer*), TokenType* ops, int num_ops) {
  ASTNode *left = sub_parser(l);
  while (1) {
    int found = 0;
    for (int i = 0; i < num_ops; i++) {
      if (current_token.type == ops[i]) {
        found = 1;
        TokenType op = current_token.type;
        eat(l, op);
        ASTNode *right = sub_parser(l);
        
        BinaryOpNode *node = calloc(1, sizeof(BinaryOpNode));
        node->base.type = NODE_BINARY_OP;
        node->op = op;
        node->left = left;
        node->right = right;
        left = (ASTNode*)node;
        break;
      }
    }
    if (!found) break;
  }
  return left;
}

ASTNode* parse_term(Lexer *l) {
  TokenType ops[] = {TOKEN_STAR, TOKEN_SLASH};
  return parse_binary_op(l, parse_unary, ops, 2);
}
ASTNode* parse_additive(Lexer *l) {
  TokenType ops[] = {TOKEN_PLUS, TOKEN_MINUS};
  return parse_binary_op(l, parse_term, ops, 2);
}
ASTNode* parse_shift(Lexer *l) {
  TokenType ops[] = {TOKEN_LSHIFT, TOKEN_RSHIFT};
  return parse_binary_op(l, parse_additive, ops, 2);
}
ASTNode* parse_relational(Lexer *l) {
  TokenType ops[] = {TOKEN_LT, TOKEN_GT, TOKEN_LTE, TOKEN_GTE};
  return parse_binary_op(l, parse_shift, ops, 4);
}
ASTNode* parse_equality(Lexer *l) {
  TokenType ops[] = {TOKEN_EQ, TOKEN_NEQ};
  return parse_binary_op(l, parse_relational, ops, 2);
}
ASTNode* parse_bitwise(Lexer *l) {
  TokenType ops[] = {TOKEN_XOR};
  return parse_binary_op(l, parse_equality, ops, 1);
}
ASTNode* parse_expression(Lexer *l) {
  return parse_bitwise(l);
}

// --- STATEMENT PARSER ---

VarType get_type_from_token(TokenType t);

ASTNode* parse_return(Lexer *l) {
  eat(l, TOKEN_RETURN);
  ASTNode *val = NULL;
  if (current_token.type != TOKEN_SEMICOLON) {
    val = parse_expression(l);
  }
  eat(l, TOKEN_SEMICOLON);
  
  ReturnNode *node = calloc(1, sizeof(ReturnNode));
  node->base.type = NODE_RETURN;
  node->value = val;
  return (ASTNode*)node;
}

ASTNode* parse_assignment_or_call(Lexer *l) {
  char *name = current_token.text;
  current_token.text = NULL;
  eat(l, TOKEN_IDENTIFIER);
  
  ASTNode *index_expr = NULL;

  if (current_token.type == TOKEN_LBRACKET) {
    eat(l, TOKEN_LBRACKET);
    index_expr = parse_expression(l);
    eat(l, TOKEN_RBRACKET);
  }

  if (current_token.type == TOKEN_ASSIGN) {
    eat(l, TOKEN_ASSIGN);
    ASTNode *expr = parse_expression(l);
    eat(l, TOKEN_SEMICOLON);

    AssignNode *node = calloc(1, sizeof(AssignNode));
    node->base.type = NODE_ASSIGN;
    node->name = name;
    node->value = expr;
    node->index = index_expr;
    return (ASTNode*)node;
  }
  
  if (index_expr) {
      parser_fail("Expected assignment after array index");
  }

  if (current_token.type == TOKEN_LPAREN) {
    ASTNode *call = parse_call(l, name);
    eat(l, TOKEN_SEMICOLON);
    return call;
  }
  
  // Command Style Call (Statement Level)
  // Check for safe argument starters to avoid confusion
  TokenType t = current_token.type;
  int is_arg_start = (t == TOKEN_NUMBER || t == TOKEN_FLOAT || t == TOKEN_STRING || 
        t == TOKEN_CHAR_LIT || t == TOKEN_TRUE || t == TOKEN_FALSE || 
        t == TOKEN_IDENTIFIER || t == TOKEN_LPAREN || t == TOKEN_LBRACKET || 
        t == TOKEN_NOT);

  if (is_arg_start) {
      ASTNode *args_head = NULL;
      ASTNode **curr_arg = &args_head;
      
      *curr_arg = parse_expression(l);
      curr_arg = &(*curr_arg)->next;

      while (current_token.type == TOKEN_COMMA) {
          eat(l, TOKEN_COMMA);
          *curr_arg = parse_expression(l);
          curr_arg = &(*curr_arg)->next;
      }
      eat(l, TOKEN_SEMICOLON);

      CallNode *node = calloc(1, sizeof(CallNode));
      node->base.type = NODE_CALL;
      node->name = name;
      node->args = args_head;
      return (ASTNode*)node;
  }

  // Implicit Variable Reference Statement "x;"
  if (current_token.type == TOKEN_SEMICOLON) {
      eat(l, TOKEN_SEMICOLON);
      VarRefNode *node = calloc(1, sizeof(VarRefNode));
      node->base.type = NODE_VAR_REF;
      node->name = name;
      return (ASTNode*)node;
  }
  
  // Fallback (maybe parser error or just a ref?)
  parser_fail("Expected assignment or function call");
  return NULL;
}

ASTNode* parse_var_decl_internal(Lexer *l) {
  int is_mut = 0; 
  
  if (current_token.type == TOKEN_KW_MUT) { is_mut = 1; eat(l, TOKEN_KW_MUT); }
  else if (current_token.type == TOKEN_KW_IMUT) { is_mut = 0; eat(l, TOKEN_KW_IMUT); }
  
  TokenType tt = current_token.type;
  VarType vtype = get_type_from_token(tt);
  if ((int)vtype == -1) { 
      parser_fail("Expected type"); 
  }
  eat(l, tt);

  if (current_token.type == TOKEN_KW_MUT) { is_mut = 1; eat(l, TOKEN_KW_MUT); }
  else if (current_token.type == TOKEN_KW_IMUT) { is_mut = 0; eat(l, TOKEN_KW_IMUT); }

  if (current_token.type != TOKEN_IDENTIFIER) { 
      parser_fail("Expected variable name"); 
  }
  char *name = current_token.text;
  current_token.text = NULL;
  eat(l, TOKEN_IDENTIFIER);
  
  int is_array = 0;
  ASTNode *array_size = NULL;
  
  if (current_token.type == TOKEN_LBRACKET) {
    is_array = 1;
    eat(l, TOKEN_LBRACKET);
    if (current_token.type != TOKEN_RBRACKET) {
      array_size = parse_expression(l);
    }
    eat(l, TOKEN_RBRACKET);
  }

  ASTNode *init = NULL;
  if (current_token.type == TOKEN_ASSIGN) {
    eat(l, TOKEN_ASSIGN);
    init = parse_expression(l);
  } else {
    if (vtype == VAR_AUTO || is_mut == 0) {
        parser_fail("Error: Immutable or 'let' variables must be initialized");
    }
  }

  eat(l, TOKEN_SEMICOLON);
  
  VarDeclNode *node = calloc(1, sizeof(VarDeclNode));
  node->base.type = NODE_VAR_DECL;
  node->var_type = vtype;
  node->name = name;
  node->initializer = init;
  node->is_mutable = is_mut;
  node->is_array = is_array;
  node->array_size = array_size;

  return (ASTNode*)node;
}

ASTNode* parse_single_statement_or_block(Lexer *l) {
  if (current_token.type == TOKEN_LBRACE) {
    eat(l, TOKEN_LBRACE);
    ASTNode *block = parse_statements(l);
    eat(l, TOKEN_RBRACE);
    return block;
  }
  
  if (current_token.type == TOKEN_LOOP) return parse_loop(l);
  if (current_token.type == TOKEN_IF) return parse_if(l);
  if (current_token.type == TOKEN_RETURN) return parse_return(l);
  
  if (get_type_from_token(current_token.type) != -1 || 
      current_token.type == TOKEN_KW_MUT || 
      current_token.type == TOKEN_KW_IMUT) {
    return parse_var_decl_internal(l);
  }
  
  if (current_token.type == TOKEN_IDENTIFIER) return parse_assignment_or_call(l);
  if (current_token.type == TOKEN_SEMICOLON) { eat(l, TOKEN_SEMICOLON); return NULL; }
  
  // FALLBACK: Parse as expression (e.g. "1 + 1;")
  ASTNode *expr = parse_expression(l);
  if (current_token.type == TOKEN_SEMICOLON) eat(l, TOKEN_SEMICOLON);
  return expr;
}

ASTNode* parse_loop(Lexer *l) {
  eat(l, TOKEN_LOOP);
  ASTNode *expr = parse_expression(l);
  LoopNode *node = calloc(1, sizeof(LoopNode));
  node->base.type = NODE_LOOP;
  node->iterations = expr;
  node->body = parse_single_statement_or_block(l);
  return (ASTNode*)node;
}

ASTNode* parse_if(Lexer *l) {
  eat(l, TOKEN_IF);
  ASTNode *cond = parse_expression(l);
  ASTNode *then_body = parse_single_statement_or_block(l);
  ASTNode *else_body = NULL;
  if (current_token.type == TOKEN_ELIF) {
    current_token.type = TOKEN_IF; 
    else_body = parse_if(l);
  } else if (current_token.type == TOKEN_ELSE) {
    eat(l, TOKEN_ELSE);
    else_body = parse_single_statement_or_block(l);
  }
  IfNode *node = calloc(1, sizeof(IfNode));
  node->base.type = NODE_IF;
  node->condition = cond;
  node->then_body = then_body;
  node->else_body = else_body;
  return (ASTNode*)node;
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

ASTNode* parse_top_level(Lexer *l) {
    // 1. IMPORT
    if (current_token.type == TOKEN_IMPORT) {
        eat(l, TOKEN_IMPORT);
        if (current_token.type != TOKEN_STRING) {
            parser_fail("Expected string after import");
        }
        char* fname = current_token.text;
        current_token.text = NULL;
        eat(l, TOKEN_STRING);
        eat(l, TOKEN_SEMICOLON);

        // Recursive Parse
        char* src = read_import_file(fname);
        if (!src) {
            fprintf(stderr, "Could not open import file: %s\n", fname);
            free(fname);
            parser_fail("Import error");
        }
        free(fname);

        Lexer import_l;
        lexer_init(&import_l, src);
        ASTNode* imported_root = parse_program(&import_l);
        return imported_root; 
    }
    
    // 2. EXTERN (FFI)
    if (current_token.type == TOKEN_EXTERN) {
        eat(l, TOKEN_EXTERN);
        
        VarType ret_type = get_type_from_token(current_token.type);
        if ((int)ret_type == -1) { parser_fail("Expected return type for extern"); }
        eat(l, current_token.type);

        if (current_token.type != TOKEN_IDENTIFIER) { parser_fail("Expected extern function name"); }
        char *name = current_token.text;
        current_token.text = NULL;
        eat(l, TOKEN_IDENTIFIER);

        eat(l, TOKEN_LPAREN);
        Parameter *params_head = NULL;
        Parameter **curr_param = &params_head;
        int is_varargs = 0;

        if (current_token.type != TOKEN_RPAREN) {
            while (1) {
                if (current_token.type == TOKEN_ELLIPSIS) {
                    eat(l, TOKEN_ELLIPSIS);
                    is_varargs = 1;
                    break; 
                }

                VarType ptype = get_type_from_token(current_token.type);
                if ((int)ptype == -1) { parser_fail("Expected param type"); }
                eat(l, current_token.type);

                char *pname = NULL;
                if (current_token.type == TOKEN_IDENTIFIER) {
                     pname = current_token.text;
                     current_token.text = NULL;
                     eat(l, TOKEN_IDENTIFIER);
                } else {
                     parser_fail("Expected param name in extern declaration");
                }
                
                Parameter *p = calloc(1, sizeof(Parameter));
                p->type = ptype; p->name = pname;
                *curr_param = p; curr_param = &p->next;
                
                if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA); else break;
            }
        }
        eat(l, TOKEN_RPAREN);
        eat(l, TOKEN_SEMICOLON);

        FuncDefNode *node = calloc(1, sizeof(FuncDefNode));
        node->base.type = NODE_FUNC_DEF;
        node->name = name; node->ret_type = ret_type;
        node->params = params_head; node->body = NULL; 
        node->is_varargs = is_varargs;
        return (ASTNode*)node;
    }

    if (current_token.type == TOKEN_KW_MUT || current_token.type == TOKEN_KW_IMUT) {
        return parse_var_decl_internal(l);
    }

    VarType vtype = get_type_from_token(current_token.type);
    
    // If it's NOT a type, it might be a statement/expression (REPL/Script mode)
    if ((int)vtype == -1) {
        return parse_single_statement_or_block(l);
    }

    eat(l, current_token.type); 
    
    if (current_token.type != TOKEN_IDENTIFIER) { 
        parser_fail("Expected identifier");
    }
    char *name = current_token.text;
    current_token.text = NULL;
    eat(l, TOKEN_IDENTIFIER);
    
    if (current_token.type == TOKEN_LPAREN) {
        // Function Definition
        eat(l, TOKEN_LPAREN);
        Parameter *params_head = NULL;
        Parameter **curr_param = &params_head;
        if (current_token.type != TOKEN_RPAREN) {
            while (1) {
                VarType ptype = get_type_from_token(current_token.type);
                eat(l, current_token.type);
                char *pname = current_token.text;
                current_token.text = NULL;
                eat(l, TOKEN_IDENTIFIER);
                
                Parameter *p = calloc(1, sizeof(Parameter));
                p->type = ptype; p->name = pname;
                *curr_param = p; curr_param = &p->next;
                
                if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA); else break;
            }
        }
        eat(l, TOKEN_RPAREN);
        eat(l, TOKEN_LBRACE);
        ASTNode *body = parse_statements(l);
        eat(l, TOKEN_RBRACE);
        
        FuncDefNode *node = calloc(1, sizeof(FuncDefNode));
        node->base.type = NODE_FUNC_DEF;
        node->name = name; node->ret_type = vtype;
        node->params = params_head; node->body = body;
        return (ASTNode*)node;
    } else {
        // Var Decl Fallback
        char *name_val = name;
        int is_array = 0;
        ASTNode *array_size = NULL;
        if (current_token.type == TOKEN_LBRACKET) {
            is_array = 1;
            eat(l, TOKEN_LBRACKET);
            if (current_token.type != TOKEN_RBRACKET) array_size = parse_expression(l);
            eat(l, TOKEN_RBRACKET);
        }
        ASTNode *init = NULL;
        if (current_token.type == TOKEN_ASSIGN) {
            eat(l, TOKEN_ASSIGN);
            init = parse_expression(l);
        } else {
             if (vtype == VAR_AUTO) { 
                 parser_fail("Init required for 'let'");
             }
        }
        eat(l, TOKEN_SEMICOLON);
        VarDeclNode *node = calloc(1, sizeof(VarDeclNode));
        node->base.type = NODE_VAR_DECL;
        node->var_type = vtype; node->name = name_val;
        node->initializer = init; node->is_mutable = 1; 
        node->is_array = is_array; node->array_size = array_size;
        return (ASTNode*)node;
    }
}

ASTNode* parse_statements(Lexer *l) {
  ASTNode *head = NULL;
  ASTNode **current = &head;

  while (current_token.type != TOKEN_EOF && current_token.type != TOKEN_RBRACE) {
    ASTNode *stmt = parse_single_statement_or_block(l);
    if (stmt) {
      *current = stmt;
      current = &stmt->next;
    }
  }
  return head;
}

ASTNode* parse_program(Lexer *l) {
  safe_free_current_token();
  current_token = lexer_next(l);
  
  ASTNode *head = NULL;
  ASTNode **current = &head;

  while (current_token.type != TOKEN_EOF) {
    ASTNode *node = parse_top_level(l);
    if (node) {
      *current = node;
      while ((*current)->next) {
          current = &(*current)->next;
      }
      current = &(*current)->next;
    }
  }
  
  safe_free_current_token();
  return head;
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
  }
  free(node);
}
