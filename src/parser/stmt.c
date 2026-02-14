#include "parser_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void set_loc(ASTNode *n, int line, int col) {
    if(n) { n->line = line; n->col = col; }
}

ASTNode* parse_return(Lexer *l) {
  int line = current_token.line, col = current_token.col;
  eat(l, TOKEN_RETURN);
  ASTNode *val = NULL;
  if (current_token.type != TOKEN_SEMICOLON) {
    val = parse_expression(l);
  }
  eat(l, TOKEN_SEMICOLON);
  ReturnNode *node = calloc(1, sizeof(ReturnNode));
  node->base.type = NODE_RETURN;
  node->value = val;
  set_loc((ASTNode*)node, line, col);
  return (ASTNode*)node;
}

ASTNode* parse_emit(Lexer *l) {
    int line = current_token.line, col = current_token.col;
    eat(l, TOKEN_EMIT);
    ASTNode *val = parse_expression(l);
    eat(l, TOKEN_SEMICOLON);
    
    EmitNode *node = calloc(1, sizeof(EmitNode));
    node->base.type = NODE_EMIT;
    node->value = val;
    set_loc((ASTNode*)node, line, col);
    return (ASTNode*)node;
}

ASTNode* parse_for_in(Lexer *l) {
    int line = current_token.line, col = current_token.col;
    eat(l, TOKEN_FOR);
    
    if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected identifier after 'for'");
    char *var_name = strdup(current_token.text);
    eat(l, TOKEN_IDENTIFIER);
    
    if (current_token.type != TOKEN_IN) parser_fail(l, "Expected 'in' after variable in for-loop");
    eat(l, TOKEN_IN);
    
    ASTNode *collection = parse_expression(l);
    ASTNode *body = parse_single_statement_or_block(l);
    
    ForInNode *node = calloc(1, sizeof(ForInNode));
    node->base.type = NODE_FOR_IN;
    node->var_name = var_name;
    node->collection = collection;
    node->body = body;
    set_loc((ASTNode*)node, line, col);
    return (ASTNode*)node;
}

ASTNode* parse_break(Lexer *l) {
    int line = current_token.line, col = current_token.col;
    eat(l, TOKEN_BREAK);
    eat(l, TOKEN_SEMICOLON);
    BreakNode *node = calloc(1, sizeof(BreakNode));
    node->base.type = NODE_BREAK;
    set_loc((ASTNode*)node, line, col);
    return (ASTNode*)node;
}

ASTNode* parse_continue(Lexer *l) {
    int line = current_token.line, col = current_token.col;
    eat(l, TOKEN_CONTINUE);
    eat(l, TOKEN_SEMICOLON);
    ContinueNode *node = calloc(1, sizeof(ContinueNode));
    node->base.type = NODE_CONTINUE;
    set_loc((ASTNode*)node, line, col);
    return (ASTNode*)node;
}

ASTNode* parse_assignment_or_call(Lexer *l) {
  Token start_token = current_token;
  if (start_token.text) start_token.text = strdup(start_token.text); 

  int line = current_token.line;
  int col = current_token.col;

  char *name = current_token.text;
  current_token.text = NULL; 
  eat(l, TOKEN_IDENTIFIER);
  
  ASTNode *node = calloc(1, sizeof(VarRefNode));
  ((VarRefNode*)node)->base.type = NODE_VAR_REF;
  ((VarRefNode*)node)->name = name;
  set_loc(node, line, col);

  if (current_token.type == TOKEN_LPAREN) {
      char *fname = ((VarRefNode*)node)->name;
      free(node); 
      node = parse_call(l, fname);
      set_loc(node, line, col); 
  }

  node = parse_postfix(l, node);

  int is_assign = 0;
  switch (current_token.type) {
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
    int op = current_token.type;
    eat(l, op); 
    ASTNode *expr = parse_expression(l);
    eat(l, TOKEN_SEMICOLON);

    AssignNode *an = calloc(1, sizeof(AssignNode));
    an->base.type = NODE_ASSIGN;
    an->value = expr;
    an->op = op;
    
    if (node->type == NODE_VAR_REF) {
        an->name = ((VarRefNode*)node)->name;
        ((VarRefNode*)node)->name = NULL; free(node);
    } else {
        an->target = node; 
    }
    set_loc((ASTNode*)an, line, col);
    if (start_token.text) free(start_token.text);
    return (ASTNode*)an;
  }
  
  if (node->type == NODE_VAR_REF) {
      TokenType t = current_token.type;
      int is_arg_start = (t == TOKEN_NUMBER || t == TOKEN_FLOAT || t == TOKEN_STRING || 
            t == TOKEN_CHAR_LIT || t == TOKEN_TRUE || t == TOKEN_FALSE || 
            t == TOKEN_IDENTIFIER || t == TOKEN_LPAREN || t == TOKEN_LBRACKET || 
            t == TOKEN_NOT || t == TOKEN_BIT_NOT || t == TOKEN_MINUS || t == TOKEN_PLUS || t == TOKEN_STAR || t == TOKEN_AND || t == TOKEN_TYPEOF);

      if (is_arg_start) {
          char *fname = ((VarRefNode*)node)->name;
          free(node); 
          
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

          CallNode *cn = calloc(1, sizeof(CallNode));
          cn->base.type = NODE_CALL;
          cn->name = fname;
          cn->args = args_head;
          set_loc((ASTNode*)cn, line, col);
          if (start_token.text) free(start_token.text);
          return (ASTNode*)cn;
      }
  }

  if (current_token.type == TOKEN_SEMICOLON) {
      eat(l, TOKEN_SEMICOLON);
      if (start_token.text) free(start_token.text);
      return node; 
  }
  
  // Error handling: Check for typos of keywords
  char msg[256];
  snprintf(msg, sizeof(msg), "Invalid statement starting with identifier '%s'.", 
           ((VarRefNode*)node)->name);
  
  const char *keyword_suggestion = find_closest_keyword(((VarRefNode*)node)->name);
  
  report_error(l, start_token, msg);
  if (keyword_suggestion) {
      char hint[128];
      snprintf(hint, sizeof(hint), "Did you mean %s?", keyword_suggestion);
      report_hint(l, start_token, hint);
  }

  if (node->type == NODE_VAR_REF) {
      if (((VarRefNode*)node)->name) free(((VarRefNode*)node)->name);
  }
  free(node);
  
  parser_error_count++;
  if (start_token.text) free(start_token.text);
  
  if (parser_recover_buf) longjmp(*parser_recover_buf, 1);
  else if (parser_env) longjmp(*parser_env, 1);
  // else exit(1);

  return NULL;
}

ASTNode* parse_var_decl_internal(Lexer *l) {
  int line = current_token.line, col = current_token.col;
  int is_mut = 1; 
  if (current_token.type == TOKEN_KW_MUT) { is_mut = 1; eat(l, TOKEN_KW_MUT); }
  else if (current_token.type == TOKEN_KW_IMUT) { is_mut = 0; eat(l, TOKEN_KW_IMUT); }
  
  VarType vtype = parse_type(l);

  if (current_token.type == TOKEN_KW_MUT) { is_mut = 1; eat(l, TOKEN_KW_MUT); }
  else if (current_token.type == TOKEN_KW_IMUT) { is_mut = 0; eat(l, TOKEN_KW_IMUT); }

  if (current_token.type != TOKEN_IDENTIFIER) { 
      parser_fail(l, "Expected variable name after type in declaration"); 
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
  set_loc((ASTNode*)node, line, col);
  return (ASTNode*)node;
}

ASTNode* parse_single_statement_or_block(Lexer *l) {
  if (current_token.type == TOKEN_LBRACE) {
    eat(l, TOKEN_LBRACE);
    ASTNode *block = parse_statements(l);
    eat(l, TOKEN_RBRACE);
    return block;
  }
  
  int line = current_token.line, col = current_token.col;

  if (current_token.type == TOKEN_LOOP) return parse_loop(l);
  if (current_token.type == TOKEN_WHILE) return parse_while(l);
  if (current_token.type == TOKEN_IF) return parse_if(l);
  if (current_token.type == TOKEN_SWITCH) return parse_switch(l);
  if (current_token.type == TOKEN_RETURN) return parse_return(l);
  if (current_token.type == TOKEN_BREAK) return parse_break(l);
  if (current_token.type == TOKEN_CONTINUE) return parse_continue(l);
  
  if (current_token.type == TOKEN_EMIT) return parse_emit(l);
  if (current_token.type == TOKEN_FOR) return parse_for_in(l);

  VarType peek_t = parse_type(l); 
  if (peek_t.base != TYPE_UNKNOWN) {
      if (peek_t.base == TYPE_CLASS && current_token.type == TOKEN_LPAREN) {
          ASTNode* call = parse_call(l, peek_t.class_name);
          eat(l, TOKEN_SEMICOLON);
          set_loc(call, line, col);
          return call;
      }

      int is_mut = 1;
      if (current_token.type == TOKEN_KW_MUT) { is_mut = 1; eat(l, TOKEN_KW_MUT); }
      else if (current_token.type == TOKEN_KW_IMUT) { is_mut = 0; eat(l, TOKEN_KW_IMUT); }
      
      if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected variable name in declaration");
      char *name = current_token.text;
      current_token.text = NULL;
      eat(l, TOKEN_IDENTIFIER);
      
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
      }
      eat(l, TOKEN_SEMICOLON);
      VarDeclNode *node = calloc(1, sizeof(VarDeclNode));
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
  
  if (current_token.type == TOKEN_KW_MUT || current_token.type == TOKEN_KW_IMUT) {
      return parse_var_decl_internal(l);
  }

  if (current_token.type == TOKEN_IDENTIFIER) return parse_assignment_or_call(l);
  
  ASTNode *expr = parse_expression(l);
  if (current_token.type == TOKEN_SEMICOLON) eat(l, TOKEN_SEMICOLON);
  return expr;
}

ASTNode* parse_loop(Lexer *l) {
  int line = current_token.line, col = current_token.col;
  eat(l, TOKEN_LOOP);
  ASTNode *expr = parse_expression(l);
  LoopNode *node = calloc(1, sizeof(LoopNode));
  node->base.type = NODE_LOOP;
  node->iterations = expr;
  node->body = parse_single_statement_or_block(l);
  set_loc((ASTNode*)node, line, col);
  return (ASTNode*)node;
}

ASTNode* parse_while(Lexer *l) {
    int line = current_token.line, col = current_token.col;
    eat(l, TOKEN_WHILE);
    int is_do_while = 0;
    if (current_token.type == TOKEN_ONCE) {
        eat(l, TOKEN_ONCE);
        is_do_while = 1;
    }
    ASTNode *cond = parse_expression(l);
    ASTNode *body = parse_single_statement_or_block(l);
    WhileNode *node = calloc(1, sizeof(WhileNode));
    node->base.type = NODE_WHILE;
    node->condition = cond;
    node->body = body;
    node->is_do_while = is_do_while;
    set_loc((ASTNode*)node, line, col);
    return (ASTNode*)node;
}

ASTNode* parse_if(Lexer *l) {
  int line = current_token.line, col = current_token.col;
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
  set_loc((ASTNode*)node, line, col);
  return (ASTNode*)node;
}

// Helper: Parse a block of statements until a Switch Case Label or End Brace is met
static ASTNode* parse_case_body_stmts(Lexer *l) {
    ASTNode *head = NULL;
    ASTNode **current = &head;
    while (current_token.type != TOKEN_EOF && 
           current_token.type != TOKEN_RBRACE && 
           current_token.type != TOKEN_CASE && 
           current_token.type != TOKEN_LEAK &&
           current_token.type != TOKEN_DEFAULT) {
        ASTNode *stmt = parse_single_statement_or_block(l);
        if (stmt) {
            *current = stmt;
            current = &stmt->next;
        }
    }
    return head;
}

ASTNode* parse_switch(Lexer *l) {
    int line = current_token.line, col = current_token.col;
    eat(l, TOKEN_SWITCH);
    
    // Condition is now parsed without mandatory parentheses
    ASTNode *cond = parse_expression(l);
    
    eat(l, TOKEN_LBRACE);

    ASTNode *cases_head = NULL;
    ASTNode **cases_curr = &cases_head;
    ASTNode *default_body = NULL;

    while (current_token.type != TOKEN_RBRACE && current_token.type != TOKEN_EOF) {
        int is_leak = 0;
        int case_line = current_token.line;
        int case_col = current_token.col;

        if (current_token.type == TOKEN_LEAK) {
            eat(l, TOKEN_LEAK);
            is_leak = 1;
        }
        
        if (current_token.type == TOKEN_CASE) {
            eat(l, TOKEN_CASE);
            
            // Comma-separated cases loop
            while(1) {
                ASTNode *val = parse_expression(l);
                
                if (current_token.type == TOKEN_COMMA) {
                    eat(l, TOKEN_COMMA);
                    // Create an implicit leak case (empty body, force leak)
                    CaseNode *cn = calloc(1, sizeof(CaseNode));
                    cn->base.type = NODE_CASE;
                    cn->value = val;
                    cn->body = NULL; 
                    cn->is_leak = 1; // Must leak to the next case in the list
                    set_loc((ASTNode*)cn, case_line, case_col);
                    
                    *cases_curr = (ASTNode*)cn;
                    cases_curr = &cn->base.next;
                } else {
                    // Last item in the comma list (or single item)
                    eat(l, TOKEN_COLON);
                    ASTNode *body = parse_case_body_stmts(l);
                    
                    CaseNode *cn = calloc(1, sizeof(CaseNode));
                    cn->base.type = NODE_CASE;
                    cn->value = val;
                    cn->body = body;
                    cn->is_leak = is_leak; // Use the originally declared leak status
                    set_loc((ASTNode*)cn, case_line, case_col);
                    
                    *cases_curr = (ASTNode*)cn;
                    cases_curr = &cn->base.next;
                    break; // Done with this case group
                }
            }
        } 
        else if (current_token.type == TOKEN_DEFAULT) {
             eat(l, TOKEN_DEFAULT);
             eat(l, TOKEN_COLON);
             if (default_body) parser_fail(l, "Duplicate default case");
             default_body = parse_case_body_stmts(l);
        } else {
            parser_fail(l, "Expected 'case', 'leak case', or 'default' inside switch");
        }
    }
    eat(l, TOKEN_RBRACE);

    SwitchNode *node = calloc(1, sizeof(SwitchNode));
    node->base.type = NODE_SWITCH;
    node->condition = cond;
    node->cases = cases_head;
    node->default_case = default_body;
    set_loc((ASTNode*)node, line, col);
    return (ASTNode*)node;
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
