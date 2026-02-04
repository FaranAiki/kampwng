#include "parser_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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

ASTNode* parse_break(Lexer *l) {
    eat(l, TOKEN_BREAK);
    eat(l, TOKEN_SEMICOLON);
    BreakNode *node = calloc(1, sizeof(BreakNode));
    node->base.type = NODE_BREAK;
    return (ASTNode*)node;
}

ASTNode* parse_continue(Lexer *l) {
    eat(l, TOKEN_CONTINUE);
    eat(l, TOKEN_SEMICOLON);
    ContinueNode *node = calloc(1, sizeof(ContinueNode));
    node->base.type = NODE_CONTINUE;
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

    AssignNode *node = calloc(1, sizeof(AssignNode));
    node->base.type = NODE_ASSIGN;
    node->name = name;
    node->value = expr;
    node->index = index_expr;
    node->op = op;
    return (ASTNode*)node;
  }
  
  if (index_expr) {
      if (current_token.type == TOKEN_INCREMENT || current_token.type == TOKEN_DECREMENT) {
          int inc_op = current_token.type;
          eat(l, inc_op);
          eat(l, TOKEN_SEMICOLON);
          
          IncDecNode *node = calloc(1, sizeof(IncDecNode));
          node->base.type = NODE_INC_DEC;
          node->name = name;
          node->index = index_expr;
          node->is_prefix = 0;
          node->op = inc_op;
          return (ASTNode*)node;
      }
      parser_fail("Expected assignment after array index");
  }

  if (current_token.type == TOKEN_LPAREN) {
    ASTNode *call = parse_call(l, name);
    eat(l, TOKEN_SEMICOLON);
    return call;
  }
  
  if (current_token.type == TOKEN_INCREMENT || current_token.type == TOKEN_DECREMENT) {
      int inc_op = current_token.type;
      eat(l, inc_op);
      eat(l, TOKEN_SEMICOLON);
      
      IncDecNode *node = calloc(1, sizeof(IncDecNode));
      node->base.type = NODE_INC_DEC;
      node->name = name;
      node->index = NULL;
      node->is_prefix = 0;
      node->op = inc_op;
      return (ASTNode*)node;
  }
  
  TokenType t = current_token.type;
  int is_arg_start = (t == TOKEN_NUMBER || t == TOKEN_FLOAT || t == TOKEN_STRING || 
        t == TOKEN_CHAR_LIT || t == TOKEN_TRUE || t == TOKEN_FALSE || 
        t == TOKEN_IDENTIFIER || t == TOKEN_LPAREN || t == TOKEN_LBRACKET || 
        t == TOKEN_NOT || t == TOKEN_BIT_NOT || t == TOKEN_MINUS || t == TOKEN_PLUS || t == TOKEN_STAR || t == TOKEN_AND);

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

  if (current_token.type == TOKEN_SEMICOLON) {
      eat(l, TOKEN_SEMICOLON);
      VarRefNode *node = calloc(1, sizeof(VarRefNode));
      node->base.type = NODE_VAR_REF;
      node->name = name;
      return (ASTNode*)node;
  }
  
  parser_fail("Expected assignment, function call, or increment/decrement");
  return NULL;
}

ASTNode* parse_var_decl_internal(Lexer *l) {
  int is_mut = 1; 
  if (current_token.type == TOKEN_KW_MUT) { is_mut = 1; eat(l, TOKEN_KW_MUT); }
  else if (current_token.type == TOKEN_KW_IMUT) { is_mut = 0; eat(l, TOKEN_KW_IMUT); }
  
  VarType vtype = parse_type(l);
  if (vtype.base == TYPE_UNKNOWN) { 
      parser_fail("Expected type"); 
  }

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
    if (vtype.base == TYPE_AUTO || is_mut == 0) {
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
  if (current_token.type == TOKEN_WHILE) return parse_while(l);
  if (current_token.type == TOKEN_IF) return parse_if(l);
  if (current_token.type == TOKEN_RETURN) return parse_return(l);
  if (current_token.type == TOKEN_BREAK) return parse_break(l);
  if (current_token.type == TOKEN_CONTINUE) return parse_continue(l);
  
  // Need to peek if it is a type start
  // This is ambiguous with identifiers. parse_type handles it.
  // We'll try parse_type. If unknown, we rollback? 
  // Lexer is not rewindable easily.
  // But parse_type only eats tokens if it MATCHES.
  // If it matches a type, we go to var decl.
  // Exception: parse_type eats nothing if it's an identifier that is NOT an alias.
  // In that case it returns TYPE_UNKNOWN.
  
  // NOTE: parse_type eats nothing if it fails match.
  VarType peek_t = parse_type(l); 
  if (peek_t.base != TYPE_UNKNOWN) {
      // It was a type, but parse_type ate it!
      // parse_var_decl_internal expects to parse the type again? 
      // No, parse_var_decl_internal calls parse_type.
      // We are in trouble because we ate the type.
      // Refactor: parse_var_decl should accept the parsed type.
      // OR: We implement a helper "parse_var_decl_with_type"
      
      int is_mut = 1;
      if (current_token.type == TOKEN_KW_MUT) { is_mut = 1; eat(l, TOKEN_KW_MUT); }
      else if (current_token.type == TOKEN_KW_IMUT) { is_mut = 0; eat(l, TOKEN_KW_IMUT); }
      
      if (current_token.type != TOKEN_IDENTIFIER) parser_fail("Expected variable name");
      char *name = current_token.text;
      current_token.text = NULL;
      eat(l, TOKEN_IDENTIFIER);
      
      // ... Copy rest of logic from parse_var_decl_internal ...
      // To avoid duplication, I will just inline it here for now.
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
        if (peek_t.base == TYPE_AUTO) parser_fail("Init required for let");
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
      return (ASTNode*)node;
  }
  
  if (current_token.type == TOKEN_KW_MUT || current_token.type == TOKEN_KW_IMUT) {
      return parse_var_decl_internal(l);
  }

  if (current_token.type == TOKEN_IDENTIFIER) return parse_assignment_or_call(l);
  if (current_token.type == TOKEN_SEMICOLON) { eat(l, TOKEN_SEMICOLON); return NULL; }
  
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

ASTNode* parse_while(Lexer *l) {
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

ASTNode* parse_statements(Lexer *l) {
  ASTNode *head = NULL;
  ASTNode **current = &head;
  while (current_token.type != TOKEN_EOF && current_token.type != TOKEN_RBRACE) {
    TokenType start_type = current_token.type;
    int start_pos = l->pos; 
    ASTNode *stmt = parse_single_statement_or_block(l);
    if (l->pos == start_pos && current_token.type == start_type) {
        if (current_token.type == TOKEN_EOF) break;
        fprintf(stderr, "Parser Error: Infinite loop detected. Force advancing.\n");
        eat(l, current_token.type); 
        continue;
    }
    if (stmt) {
      *current = stmt;
      current = &stmt->next;
    }
  }
  return head;
}
