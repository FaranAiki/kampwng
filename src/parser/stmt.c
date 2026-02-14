#include "parser_internal.h"
#include <string.h>
#include <stdlib.h>

void parse_block(Lexer *l, ASTNode **head);

ASTNode* parse_if(Lexer *l) {
  eat(l, TOKEN_IF);
  eat(l, TOKEN_LPAREN);
  ASTNode *cond = parse_expression(l);
  eat(l, TOKEN_RPAREN);
  ASTNode *then_body = NULL;
  
  if (current_token.type == TOKEN_LBRACE) {
      eat(l, TOKEN_LBRACE);
      parse_block(l, &then_body);
      eat(l, TOKEN_RBRACE);
  } else {
      then_body = parse_statement(l);
  }
  
  ASTNode *else_body = NULL;
  if (current_token.type == TOKEN_ELSE) {
    eat(l, TOKEN_ELSE);
    if (current_token.type == TOKEN_LBRACE) {
        eat(l, TOKEN_LBRACE);
        parse_block(l, &else_body);
        eat(l, TOKEN_RBRACE);
    } else {
        else_body = parse_statement(l);
    }
  }
  
  IfNode *node = calloc(1, sizeof(IfNode));
  node->base.type = NODE_IF;
  node->condition = cond;
  node->then_body = then_body;
  node->else_body = else_body;
  return (ASTNode*)node;
}

ASTNode* parse_switch(Lexer *l) {
    eat(l, TOKEN_SWITCH);
    eat(l, TOKEN_LPAREN);
    ASTNode *cond = parse_expression(l);
    eat(l, TOKEN_RPAREN);
    eat(l, TOKEN_LBRACE);
    
    ASTNode *cases_head = NULL;
    ASTNode **curr_case = &cases_head;
    ASTNode *default_body = NULL;
    
    while(current_token.type != TOKEN_RBRACE && current_token.type != TOKEN_EOF) {
        if (current_token.type == TOKEN_CASE) {
            eat(l, TOKEN_CASE);
            ASTNode *val = parse_expression(l);
            eat(l, TOKEN_COLON);
            
            CaseNode *cn = calloc(1, sizeof(CaseNode));
            cn->base.type = NODE_CASE;
            cn->value = val;
            
            // Parse statements until next case/default/rbrace
            ASTNode *body_head = NULL;
            ASTNode **curr_stmt = &body_head;
            while (current_token.type != TOKEN_CASE && 
                   current_token.type != TOKEN_DEFAULT && 
                   current_token.type != TOKEN_RBRACE &&
                   current_token.type != TOKEN_EOF) {
                 *curr_stmt = parse_statement(l);
                 if (*curr_stmt) curr_stmt = &(*curr_stmt)->next;
            }
            cn->body = body_head;
            
            *curr_case = (ASTNode*)cn;
            curr_case = &(*curr_case)->next;
        } else if (current_token.type == TOKEN_DEFAULT) {
            eat(l, TOKEN_DEFAULT);
            eat(l, TOKEN_COLON);
            
            ASTNode *body_head = NULL;
            ASTNode **curr_stmt = &body_head;
            while (current_token.type != TOKEN_CASE && 
                   current_token.type != TOKEN_DEFAULT && 
                   current_token.type != TOKEN_RBRACE &&
                   current_token.type != TOKEN_EOF) {
                 *curr_stmt = parse_statement(l);
                 if (*curr_stmt) curr_stmt = &(*curr_stmt)->next;
            }
            default_body = body_head;
        } else {
            // Error recovery
            eat(l, current_token.type);
        }
    }
    eat(l, TOKEN_RBRACE);
    
    SwitchNode *sn = calloc(1, sizeof(SwitchNode));
    sn->base.type = NODE_SWITCH;
    sn->condition = cond;
    sn->cases = cases_head;
    sn->default_case = default_body;
    return (ASTNode*)sn;
}

ASTNode* parse_while(Lexer *l) {
  eat(l, TOKEN_WHILE);
  eat(l, TOKEN_LPAREN);
  ASTNode *cond = parse_expression(l);
  eat(l, TOKEN_RPAREN);
  ASTNode *body = NULL;
  
  if (current_token.type == TOKEN_LBRACE) {
      eat(l, TOKEN_LBRACE);
      parse_block(l, &body);
      eat(l, TOKEN_RBRACE);
  } else {
      body = parse_statement(l);
  }

  WhileNode *node = calloc(1, sizeof(WhileNode));
  node->base.type = NODE_WHILE;
  node->condition = cond;
  node->body = body;
  return (ASTNode*)node;
}

ASTNode* parse_for(Lexer *l) {
    eat(l, TOKEN_FOR);
    eat(l, TOKEN_LPAREN);
    
    // Check if it's "for var in collection"
    // Heuristic: Look ahead. If we see "identifier in", it's a foreach.
    // Since we don't have infinite lookahead, we parse the type/var name first.
    
    // Standard Loop: loop (iterations) { }
    // We already have 'loop' keyword, so 'for' usually implies standard C-style or range
    // Alkyl 'for' is "for (var : collection)" or "for (init; cond; inc)"
    
    // Attempt C-style first:
    // This is tricky without backtracking. 
    // Let's support: for (i : 0..10) or for (i in collection)
    // Actually, let's implement the simpler "loop" node for numeric iteration 
    // and assume "for" handles iterator-based.
    
    // Simplified For-In: for (x in arr)
    if (current_token.type == TOKEN_IDENTIFIER) {
        char *var_name = strdup(current_token.text);
        eat(l, TOKEN_IDENTIFIER);
        
        if (current_token.type == TOKEN_IN) {
            eat(l, TOKEN_IN);
            ASTNode *col = parse_expression(l);
            eat(l, TOKEN_RPAREN);
            
            ASTNode *body = NULL;
            if (current_token.type == TOKEN_LBRACE) {
                eat(l, TOKEN_LBRACE);
                parse_block(l, &body);
                eat(l, TOKEN_RBRACE);
            } else {
                body = parse_statement(l);
            }
            
            ForInNode *fn = calloc(1, sizeof(ForInNode));
            fn->base.type = NODE_FOR_IN;
            fn->var_name = var_name;
            fn->collection = col;
            fn->body = body;
            return (ASTNode*)fn;
        } else {
             parser_fail(l, "Expected 'in' after variable in for-loop (C-style for not supported yet, use 'loop' or 'while')");
        }
    }
    
    parser_fail(l, "Invalid for-loop syntax");
    return NULL;
}

ASTNode* parse_loop(Lexer *l) {
    eat(l, TOKEN_LOOP);
    ASTNode *iter = NULL;
    if (current_token.type == TOKEN_LPAREN) {
        eat(l, TOKEN_LPAREN);
        iter = parse_expression(l);
        eat(l, TOKEN_RPAREN);
    }
    ASTNode *body = NULL;
    if (current_token.type == TOKEN_LBRACE) {
        eat(l, TOKEN_LBRACE);
        parse_block(l, &body);
        eat(l, TOKEN_RBRACE);
    } else {
        body = parse_statement(l);
    }
    LoopNode *node = calloc(1, sizeof(LoopNode));
    node->base.type = NODE_LOOP;
    node->iterations = iter;
    node->body = body;
    return (ASTNode*)node;
}

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

ASTNode* parse_var_decl(Lexer *l, VarType type) {
  char *name = current_token.text;
  eat(l, TOKEN_IDENTIFIER);
  
  int is_array = 0;
  ASTNode *arr_size = NULL;
  
  // Updated: Handle multi-dimensional arrays (e.g., [2][4])
  // We treat int a[2][4] as equivalent to int *a[2] in terms of type depth
  // The first dimension sets is_array/array_size.
  // Subsequent dimensions increase ptr_depth.
  
  while (current_token.type == TOKEN_LBRACKET) {
      eat(l, TOKEN_LBRACKET);
      
      ASTNode *dim_expr = NULL;
      // Parse dimension size
      if (current_token.type != TOKEN_RBRACKET) {
          dim_expr = parse_expression(l);
      }
      
      if (!is_array) {
          is_array = 1;
          arr_size = dim_expr; // Store first dimension size
      } else {
          // Inner dimension: increase pointer depth
          // e.g., int[2][4] -> array of 2 pointers to int
          type.ptr_depth++;
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
  node->var_type = type;
  node->name = name;
  node->is_array = is_array;
  node->array_size = arr_size;
  node->initializer = init;
  return (ASTNode*)node;
}

ASTNode* parse_statement(Lexer *l) {
  if (current_token.type == TOKEN_IF) return parse_if(l);
  if (current_token.type == TOKEN_WHILE) return parse_while(l);
  if (current_token.type == TOKEN_LOOP) return parse_loop(l);
  if (current_token.type == TOKEN_FOR) return parse_for(l);
  if (current_token.type == TOKEN_SWITCH) return parse_switch(l);
  if (current_token.type == TOKEN_RETURN) return parse_return(l);
  
  if (current_token.type == TOKEN_BREAK) {
      eat(l, TOKEN_BREAK);
      eat(l, TOKEN_SEMICOLON);
      ASTNode *n = calloc(1, sizeof(ASTNode));
      n->type = NODE_BREAK;
      return n;
  }
  if (current_token.type == TOKEN_CONTINUE) {
      eat(l, TOKEN_CONTINUE);
      eat(l, TOKEN_SEMICOLON);
      ASTNode *n = calloc(1, sizeof(ASTNode));
      n->type = NODE_CONTINUE;
      return n;
  }
  
  if (current_token.type == TOKEN_EMIT || current_token.type == TOKEN_YIELD) {
      eat(l, current_token.type);
      ASTNode *val = parse_expression(l);
      eat(l, TOKEN_SEMICOLON);
      EmitNode *n = calloc(1, sizeof(EmitNode));
      n->base.type = NODE_EMIT;
      n->value = val;
      return (ASTNode*)n;
  }

  // Check for variable declaration
  VarType type;
  int is_decl = 0;
  
  // Lookahead hack or just check standard types
  // Note: is_typename should be robust
  if (current_token.type == TOKEN_IDENTIFIER) {
      if (is_typename(current_token.text)) {
          type = parse_type(l);
          if (current_token.type == TOKEN_IDENTIFIER) {
               return parse_var_decl(l, type);
          }
      }
  } else if (current_token.type == TOKEN_INT || current_token.type == TOKEN_FLOAT || 
             current_token.type == TOKEN_CHAR || current_token.type == TOKEN_STRING || 
             current_token.type == TOKEN_BOOL || current_token.type == TOKEN_VOID ||
             current_token.type == TOKEN_AUTO || current_token.type == TOKEN_LONG ||
             current_token.type == TOKEN_DOUBLE) {
       type = parse_type(l);
       return parse_var_decl(l, type);
  }

  // Expression statement
  ASTNode *expr = parse_expression(l);
  eat(l, TOKEN_SEMICOLON);
  return expr;
}

void parse_block(Lexer *l, ASTNode **head) {
    ASTNode **curr = head;
    while (current_token.type != TOKEN_RBRACE && current_token.type != TOKEN_EOF) {
        *curr = parse_statement(l);
        if (*curr) curr = &(*curr)->next;
    }
}
