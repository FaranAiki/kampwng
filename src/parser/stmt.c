#include "parser_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static void eat_semi(Parser *p) {
    if (p->current_token.type == TOKEN_SEMICOLON) {
        eat(p, TOKEN_SEMICOLON);
    } else if (p->current_token.type == TOKEN_ELSE || 
               p->current_token.type == TOKEN_ELIF || 
               p->current_token.type == TOKEN_RBRACE || 
               p->current_token.type == TOKEN_EOF) {
        // Implicit
    } else {
        eat(p, TOKEN_SEMICOLON);
    }
}

static void set_loc(ASTNode *n, int line, int col) {
    if(n) { n->line = line; n->col = col; }
}

ASTNode* parse_return(Parser *p) {
  int line = p->current_token.line, col = p->current_token.col;
  eat(p, TOKEN_RETURN);
  ASTNode *val = NULL;
  if (p->current_token.type != TOKEN_SEMICOLON && 
      p->current_token.type != TOKEN_ELSE && 
      p->current_token.type != TOKEN_ELIF && 
      p->current_token.type != TOKEN_RBRACE && 
      p->current_token.type != TOKEN_EOF) {
    val = parse_expression(p);
  }
  eat_semi(p);
  ReturnNode *node = parser_alloc(p, sizeof(ReturnNode));
  node->base.type = NODE_RETURN;
  node->value = val;
  set_loc((ASTNode*)node, line, col);
  return (ASTNode*)node;
}

ASTNode* parse_emit(Parser *p) {
    int line = p->current_token.line, col = p->current_token.col;
    eat(p, TOKEN_EMIT);
    ASTNode *val = parse_expression(p);
    eat_semi(p);
    
    EmitNode *node = parser_alloc(p, sizeof(EmitNode));
    node->base.type = NODE_EMIT;
    node->value = val;
    set_loc((ASTNode*)node, line, col);
    return (ASTNode*)node;
}

ASTNode* parse_for_in(Parser *p) {
    int line = p->current_token.line, col = p->current_token.col;
    eat(p, TOKEN_FOR);
    
    if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected identifier after 'for'");
    char *var_name = parser_strdup(p, p->current_token.text);
    eat(p, TOKEN_IDENTIFIER);
    
    if (p->current_token.type != TOKEN_IN) parser_fail(p, "Expected 'in' after variable in for-loop");
    eat(p, TOKEN_IN);
    
    ASTNode *collection = parse_expression(p);
    ASTNode *body = parse_single_statement_or_block(p);
    
    ForInNode *node = parser_alloc(p, sizeof(ForInNode));
    node->base.type = NODE_FOR_IN;
    node->var_name = var_name;
    node->collection = collection;
    node->body = body;
    set_loc((ASTNode*)node, line, col);
    return (ASTNode*)node;
}

ASTNode* parse_break(Parser *p) {
    int line = p->current_token.line, col = p->current_token.col;
    eat(p, TOKEN_BREAK);
    eat_semi(p);
    BreakNode *node = parser_alloc(p, sizeof(BreakNode));
    node->base.type = NODE_BREAK;
    set_loc((ASTNode*)node, line, col);
    return (ASTNode*)node;
}

ASTNode* parse_continue(Parser *p) {
    int line = p->current_token.line, col = p->current_token.col;
    eat(p, TOKEN_CONTINUE);
    eat_semi(p);
    ContinueNode *node = parser_alloc(p, sizeof(ContinueNode));
    node->base.type = NODE_CONTINUE;
    set_loc((ASTNode*)node, line, col);
    return (ASTNode*)node;
}

ASTNode* parse_assignment_or_call(Parser *p) {
  Token start_token = p->current_token;
  if (start_token.text) start_token.text = parser_strdup(p, start_token.text); 

  int line = p->current_token.line;
  int col = p->current_token.col;

  char *name = p->current_token.text; // already arena alloc from lexer/strdup
  p->current_token.text = NULL; 
  eat(p, TOKEN_IDENTIFIER);
  
  ASTNode *node = parser_alloc(p, sizeof(VarRefNode));
  ((VarRefNode*)node)->base.type = NODE_VAR_REF;
  ((VarRefNode*)node)->name = name;
  set_loc(node, line, col);

  if (p->current_token.type == TOKEN_LPAREN) {
      char *fname = ((VarRefNode*)node)->name;
      // No free(node) with arena
      node = parse_call(p, fname);
      set_loc(node, line, col); 
  }

  node = parse_postfix(p, node);

  int is_assign = 0;
  switch (p->current_token.type) {
      case TOKEN_ASSIGN:
      case TOKEN_PLUS_ASSIGN:
      case TOKEN_MINUS_ASSIGN:
      case TOKEN_STAR_ASSIGN:
      case TOKEN_SLASH_ASSIGN:
      case TOKEN_MOD_ASSIGN:
      case TOKEN_AND_ASSIGN:
      case TOKEN_OR_ASSIGN:
      case TOKEN_XOR_ASSIGN:
      case TOKEN_LSHIFT_ASSIGN:
      case TOKEN_RSHIFT_ASSIGN:
          is_assign = 1;
          break;
      default:
          is_assign = 0;
  }

  if (is_assign) {
    int op = p->current_token.type;
    eat(p, op); 
    ASTNode *expr = parse_expression(p);
    eat_semi(p); 

    AssignNode *an = parser_alloc(p, sizeof(AssignNode));
    an->base.type = NODE_ASSIGN;
    an->value = expr;
    an->op = op;
    
    if (node->type == NODE_VAR_REF) {
        an->name = ((VarRefNode*)node)->name;
        ((VarRefNode*)node)->name = NULL; 
        // No free(node)
    } else {
        an->target = node; 
    }
    set_loc((ASTNode*)an, line, col);
    // No free(start_token.text)
    return (ASTNode*)an;
  }
  
  if (node->type == NODE_VAR_REF) {
      TokenType t = p->current_token.type;
      int is_arg_start = (t == TOKEN_NUMBER || t == TOKEN_FLOAT || t == TOKEN_STRING || 
            t == TOKEN_CHAR_LIT || t == TOKEN_TRUE || t == TOKEN_FALSE || 
            t == TOKEN_IDENTIFIER || t == TOKEN_LPAREN || t == TOKEN_LBRACKET || 
            t == TOKEN_NOT || t == TOKEN_BIT_NOT || t == TOKEN_MINUS || t == TOKEN_PLUS || t == TOKEN_STAR || t == TOKEN_AND || t == TOKEN_TYPEOF);

      if (is_arg_start) {
          char *fname = ((VarRefNode*)node)->name;
          // No free(node)
          
          ASTNode *args_head = NULL;
          ASTNode **curr_arg = &args_head;
          
          *curr_arg = parse_expression(p);
          curr_arg = &(*curr_arg)->next;

          while (p->current_token.type == TOKEN_COMMA) {
              eat(p, TOKEN_COMMA);
              *curr_arg = parse_expression(p);
              curr_arg = &(*curr_arg)->next;
          }
          eat_semi(p); 

          CallNode *cn = parser_alloc(p, sizeof(CallNode));
          cn->base.type = NODE_CALL;
          cn->name = fname;
          cn->args = args_head;
          set_loc((ASTNode*)cn, line, col);
          // No free
          return (ASTNode*)cn;
      }
  }

  if (p->current_token.type == TOKEN_SEMICOLON || 
      p->current_token.type == TOKEN_ELSE || 
      p->current_token.type == TOKEN_ELIF || 
      p->current_token.type == TOKEN_RBRACE || 
      p->current_token.type == TOKEN_EOF) {
      
      eat_semi(p);
      // No free
      return node; 
  }
  
  char msg[256];
  snprintf(msg, sizeof(msg), "Invalid statement starting with identifier '%s'.", 
           ((VarRefNode*)node)->name);
  
  const char *keyword_suggestion = find_closest_keyword(((VarRefNode*)node)->name);
  
  // Custom fail reporting to avoid exit
  report_error(p->l, start_token, msg);
  if (keyword_suggestion) {
      char hint[128];
      snprintf(hint, sizeof(hint), "Did you mean %s?", keyword_suggestion);
      report_hint(p->l, start_token, hint);
  }

  // No frees needed
  
  if (p->ctx) p->ctx->error_count++;
  
  if (p->recover_buf) longjmp(*p->recover_buf, 1);
  else exit(1);

  return NULL;
}

ASTNode* parse_var_decl_internal(Parser *p) {
  int line = p->current_token.line, col = p->current_token.col;
  int is_mut = 1; 
  if (p->current_token.type == TOKEN_KW_MUT) { is_mut = 1; eat(p, TOKEN_KW_MUT); }
  else if (p->current_token.type == TOKEN_KW_IMUT) { is_mut = 0; eat(p, TOKEN_KW_IMUT); }
  
  VarType vtype = parse_type(p);

  if (p->current_token.type == TOKEN_LPAREN) {
      char *name = NULL;
      vtype = parse_func_ptr_decl(p, vtype, &name);
      
      ASTNode *init = NULL;
      if (p->current_token.type == TOKEN_ASSIGN) {
          eat(p, TOKEN_ASSIGN);
          init = parse_expression(p);
      }
      eat_semi(p);
      
      VarDeclNode *node = parser_alloc(p, sizeof(VarDeclNode));
      node->base.type = NODE_VAR_DECL;
      node->var_type = vtype;
      node->name = name;
      node->initializer = init;
      node->is_mutable = is_mut;
      set_loc((ASTNode*)node, line, col);
      return (ASTNode*)node;
  }

  if (p->current_token.type == TOKEN_KW_MUT) { is_mut = 1; eat(p, TOKEN_KW_MUT); }
  else if (p->current_token.type == TOKEN_KW_IMUT) { is_mut = 0; eat(p, TOKEN_KW_IMUT); }

  if (p->current_token.type != TOKEN_IDENTIFIER) { 
      parser_fail(p, "Expected variable name after type in declaration"); 
  }
  char *name = p->current_token.text;
  p->current_token.text = NULL;
  eat(p, TOKEN_IDENTIFIER);
  
  int is_array = 0;
  ASTNode *array_size = NULL;
  ASTNode **curr_sz = &array_size;
  
  while (p->current_token.type == TOKEN_LBRACKET) {
    is_array = 1;
    vtype.ptr_depth++; 
    eat(p, TOKEN_LBRACKET);
    ASTNode *sz = NULL;
    if (p->current_token.type != TOKEN_RBRACKET) {
      sz = parse_expression(p);
    } else {
        LiteralNode *ln = parser_alloc(p, sizeof(LiteralNode));
        ln->base.type = NODE_LITERAL;
        ln->var_type.base = TYPE_INT;
        ln->val.int_val = 0;
        sz = (ASTNode*)ln;
    }
    *curr_sz = sz;
    curr_sz = &sz->next;
    eat(p, TOKEN_RBRACKET);
  }

  ASTNode *init = NULL;
  if (p->current_token.type == TOKEN_ASSIGN) {
    eat(p, TOKEN_ASSIGN);
    init = parse_expression(p);
  }

  eat_semi(p);
  
  VarDeclNode *node = parser_alloc(p, sizeof(VarDeclNode));
  node->base.type = NODE_VAR_DECL;
  node->var_type = vtype;
  node->name = name;
  node->initializer = init;
  node->is_mutable = is_mut;
  node->is_array = is_array;
  node->array_size = array_size;
  set_loc((ASTNode*)node, line, col);
  return (ASTNode*)node;
}

ASTNode* parse_single_statement_or_block(Parser *p) {
  if (p->current_token.type == TOKEN_LBRACE) {
    eat(p, TOKEN_LBRACE);
    ASTNode *block = parse_statements(p);
    eat(p, TOKEN_RBRACE);
    return block;
  }
  
  int line = p->current_token.line, col = p->current_token.col;

  if (p->current_token.type == TOKEN_LOOP) return parse_loop(p);
  if (p->current_token.type == TOKEN_WHILE) return parse_while(p);
  if (p->current_token.type == TOKEN_IF) return parse_if(p);
  if (p->current_token.type == TOKEN_SWITCH) return parse_switch(p);
  if (p->current_token.type == TOKEN_RETURN) return parse_return(p);
  if (p->current_token.type == TOKEN_BREAK) return parse_break(p);
  if (p->current_token.type == TOKEN_CONTINUE) return parse_continue(p);
  
  if (p->current_token.type == TOKEN_EMIT) return parse_emit(p);
  if (p->current_token.type == TOKEN_FOR) return parse_for_in(p);

  VarType peek_t = parse_type(p); 
  if (peek_t.base != TYPE_UNKNOWN) {
      if (peek_t.base == TYPE_CLASS && p->current_token.type == TOKEN_LPAREN) {
          ASTNode* call = parse_call(p, peek_t.class_name);
          eat(p, TOKEN_SEMICOLON);
          set_loc(call, line, col);
          return call;
      }
      
      if (p->current_token.type == TOKEN_LPAREN) {
          char *name = NULL;
          VarType fp_type = parse_func_ptr_decl(p, peek_t, &name);
          
          ASTNode *init = NULL;
          if (p->current_token.type == TOKEN_ASSIGN) {
              eat(p, TOKEN_ASSIGN);
              init = parse_expression(p);
          }
          eat_semi(p);
          
          VarDeclNode *node = parser_alloc(p, sizeof(VarDeclNode));
          node->base.type = NODE_VAR_DECL;
          node->var_type = fp_type;
          node->name = name;
          node->initializer = init;
          node->is_mutable = 1; 
          set_loc((ASTNode*)node, line, col);
          return (ASTNode*)node;
      }

      int is_mut = 1;
      if (p->current_token.type == TOKEN_KW_MUT) { is_mut = 1; eat(p, TOKEN_KW_MUT); }
      else if (p->current_token.type == TOKEN_KW_IMUT) { is_mut = 0; eat(p, TOKEN_KW_IMUT); }
      
      if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected variable name in declaration");
      char *name = p->current_token.text;
      p->current_token.text = NULL;
      eat(p, TOKEN_IDENTIFIER);
      
      int is_array = 0;
      ASTNode *array_size = NULL;
      ASTNode **curr_sz = &array_size;
      
      while (p->current_token.type == TOKEN_LBRACKET) {
        is_array = 1;
        peek_t.ptr_depth++; 
        eat(p, TOKEN_LBRACKET);
        ASTNode *sz = NULL;
        if (p->current_token.type != TOKEN_RBRACKET) sz = parse_expression(p);
        else {
             LiteralNode *ln = parser_alloc(p, sizeof(LiteralNode));
             ln->base.type = NODE_LITERAL;
             ln->var_type.base = TYPE_INT;
             ln->val.int_val = 0;
             sz = (ASTNode*)ln;
        }
        *curr_sz = sz;
        curr_sz = &sz->next;
        eat(p, TOKEN_RBRACKET);
      }

      ASTNode *init = NULL;
      if (p->current_token.type == TOKEN_ASSIGN) {
        eat(p, TOKEN_ASSIGN);
        init = parse_expression(p);
      }
      eat_semi(p);
      VarDeclNode *node = parser_alloc(p, sizeof(VarDeclNode));
      node->base.type = NODE_VAR_DECL;
      node->var_type = peek_t;
      node->name = name;
      node->initializer = init;
      node->is_mutable = is_mut;
      node->is_array = is_array;
      node->array_size = array_size;
      set_loc((ASTNode*)node, line, col);
      return (ASTNode*)node;
  }
  
  if (p->current_token.type == TOKEN_KW_MUT || p->current_token.type == TOKEN_KW_IMUT) {
      return parse_var_decl_internal(p);
  }

  if (p->current_token.type == TOKEN_IDENTIFIER) return parse_assignment_or_call(p);
  
  ASTNode *expr = parse_expression(p);
  eat_semi(p); 
  return expr;
}

ASTNode* parse_loop(Parser *p) {
  int line = p->current_token.line, col = p->current_token.col;
  eat(p, TOKEN_LOOP);
  ASTNode *expr = parse_expression(p);
  LoopNode *node = parser_alloc(p, sizeof(LoopNode));
  node->base.type = NODE_LOOP;
  node->iterations = expr;
  node->body = parse_single_statement_or_block(p);
  set_loc((ASTNode*)node, line, col);
  return (ASTNode*)node;
}

ASTNode* parse_while(Parser *p) {
    int line = p->current_token.line, col = p->current_token.col;
    eat(p, TOKEN_WHILE);
    int is_do_while = 0;
    if (p->current_token.type == TOKEN_ONCE) {
        eat(p, TOKEN_ONCE);
        is_do_while = 1;
    }
    ASTNode *cond = parse_expression(p);
    ASTNode *body = parse_single_statement_or_block(p);
    WhileNode *node = parser_alloc(p, sizeof(WhileNode));
    node->base.type = NODE_WHILE;
    node->condition = cond;
    node->body = body;
    node->is_do_while = is_do_while;
    set_loc((ASTNode*)node, line, col);
    return (ASTNode*)node;
}

ASTNode* parse_if(Parser *p) {
  int line = p->current_token.line, col = p->current_token.col;
  eat(p, TOKEN_IF);
  ASTNode *cond = parse_expression(p);
  
  if (p->current_token.type == TOKEN_THEN) {
      eat(p, TOKEN_THEN);
  }
  
  ASTNode *then_body = parse_single_statement_or_block(p);
  ASTNode *else_body = NULL;
  if (p->current_token.type == TOKEN_ELIF) {
    p->current_token.type = TOKEN_IF; 
    else_body = parse_if(p);
  } else if (p->current_token.type == TOKEN_ELSE) {
    eat(p, TOKEN_ELSE);
    else_body = parse_single_statement_or_block(p);
  }
  IfNode *node = parser_alloc(p, sizeof(IfNode));
  node->base.type = NODE_IF;
  node->condition = cond;
  node->then_body = then_body;
  node->else_body = else_body;
  set_loc((ASTNode*)node, line, col);
  return (ASTNode*)node;
}

static ASTNode* parse_case_body_stmts(Parser *p) {
    ASTNode *head = NULL;
    ASTNode **current = &head;
    while (p->current_token.type != TOKEN_EOF && 
           p->current_token.type != TOKEN_RBRACE && 
           p->current_token.type != TOKEN_CASE && 
           p->current_token.type != TOKEN_LEAK &&
           p->current_token.type != TOKEN_DEFAULT) {
        ASTNode *stmt = parse_single_statement_or_block(p);
        if (stmt) {
            *current = stmt;
            current = &stmt->next;
        }
    }
    // *current = NULL; // don't have to cut off
    return head;
}

ASTNode* parse_switch(Parser *p) {
    int line = p->current_token.line, col = p->current_token.col;
    eat(p, TOKEN_SWITCH);
    
    ASTNode *cond = parse_expression(p);
    
    eat(p, TOKEN_LBRACE);

    ASTNode *cases_head = NULL;
    ASTNode **cases_curr = &cases_head;
    ASTNode *default_body = NULL;

    while (p->current_token.type != TOKEN_RBRACE && p->current_token.type != TOKEN_EOF) {
        int is_leak = 0;
        int case_line = p->current_token.line;
        int case_col = p->current_token.col;

        if (p->current_token.type == TOKEN_LEAK) {
            eat(p, TOKEN_LEAK);
            is_leak = 1;
        }
        
        if (p->current_token.type == TOKEN_CASE) {
            eat(p, TOKEN_CASE);
            
            while(1) {
                ASTNode *val = parse_expression(p);
                
                if (p->current_token.type == TOKEN_COMMA) {
                    eat(p, TOKEN_COMMA);
                    CaseNode *cn = parser_alloc(p, sizeof(CaseNode));
                    cn->base.type = NODE_CASE;
                    cn->value = val;
                    cn->body = NULL; 
                    cn->is_leak = 1; 
                    set_loc((ASTNode*)cn, case_line, case_col);
                    
                    *cases_curr = (ASTNode*)cn;
                    cases_curr = &cn->base.next;
                } else {
                    eat(p, TOKEN_COLON);
                    ASTNode *body = parse_case_body_stmts(p);
                    
                    CaseNode *cn = parser_alloc(p, sizeof(CaseNode));
                    cn->base.type = NODE_CASE;
                    cn->value = val;
                    cn->body = body;
                    cn->is_leak = is_leak; 
                    set_loc((ASTNode*)cn, case_line, case_col);
                    
                    *cases_curr = (ASTNode*)cn;
                    cases_curr = &cn->base.next;
                    break; 
                }
            }
        } 
        else if (p->current_token.type == TOKEN_DEFAULT) {
             eat(p, TOKEN_DEFAULT);
             eat(p, TOKEN_COLON);
             if (default_body) parser_fail(p, "Duplicate default case");
             default_body = parse_case_body_stmts(p);
        } else {
            parser_fail(p, "Expected 'case', 'leak case', or 'default' inside switch");
        }
    }
    eat(p, TOKEN_RBRACE);

    SwitchNode *node = parser_alloc(p, sizeof(SwitchNode));
    node->base.type = NODE_SWITCH;
    node->condition = cond;
    node->cases = cases_head;
    node->default_case = default_body;
    set_loc((ASTNode*)node, line, col);
    return (ASTNode*)node;
}

ASTNode* parse_statements(Parser *p) {
  ASTNode *head = NULL;
  ASTNode **current = &head;
  while (p->current_token.type != TOKEN_EOF && p->current_token.type != TOKEN_RBRACE) {
    ASTNode *stmt = parse_single_statement_or_block(p);
    if (stmt) {
      *current = stmt;
      current = &stmt->next;
    }
  }
  return head;
}
