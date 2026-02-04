#include "parser_internal.h"
#include <string.h>
#include <stdlib.h>

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
    
    ASTNode *index_expr = NULL;
    if (current_token.type == TOKEN_LBRACKET) {
      eat(l, TOKEN_LBRACKET);
      index_expr = parse_expression(l);
      eat(l, TOKEN_RBRACKET);
    }
    
    // Check for Postfix ++/--
    if (current_token.type == TOKEN_INCREMENT || current_token.type == TOKEN_DECREMENT) {
        int op = current_token.type;
        eat(l, op);
        
        IncDecNode *node = calloc(1, sizeof(IncDecNode));
        node->base.type = NODE_INC_DEC;
        node->name = name;
        node->index = index_expr;
        node->is_prefix = 0;
        node->op = op;
        return (ASTNode*)node;
    }
    
    if (index_expr) {
      ArrayAccessNode *node = calloc(1, sizeof(ArrayAccessNode));
      node->base.type = NODE_ARRAY_ACCESS;
      node->name = name;
      node->index = index_expr;
      return (ASTNode*)node;
    }
    
    // Command Style Call (Expression Level)
    TokenType t = current_token.type;
    int is_arg_start = (t == TOKEN_NUMBER || t == TOKEN_FLOAT || t == TOKEN_STRING || 
          t == TOKEN_CHAR_LIT || t == TOKEN_TRUE || t == TOKEN_FALSE || 
          t == TOKEN_IDENTIFIER || t == TOKEN_LPAREN || t == TOKEN_LBRACKET || 
          t == TOKEN_NOT || t == TOKEN_BIT_NOT || t == TOKEN_MINUS);

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
  if (current_token.type == TOKEN_INCREMENT || current_token.type == TOKEN_DECREMENT) {
      // Prefix ++/--
      int op = current_token.type;
      eat(l, op);
      
      // Expect identifier or array access
      if (current_token.type != TOKEN_IDENTIFIER) {
          parser_fail("Expected identifier after ++/--");
      }
      char *name = current_token.text;
      current_token.text = NULL;
      eat(l, TOKEN_IDENTIFIER);
      
      ASTNode *index_expr = NULL;
      if (current_token.type == TOKEN_LBRACKET) {
          eat(l, TOKEN_LBRACKET);
          index_expr = parse_expression(l);
          eat(l, TOKEN_RBRACKET);
      }
      
      IncDecNode *node = calloc(1, sizeof(IncDecNode));
      node->base.type = NODE_INC_DEC;
      node->name = name;
      node->index = index_expr;
      node->is_prefix = 1;
      node->op = op;
      return (ASTNode*)node;
  }
  
  if (current_token.type == TOKEN_NOT || current_token.type == TOKEN_MINUS || current_token.type == TOKEN_BIT_NOT) {
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
  TokenType ops[] = {TOKEN_STAR, TOKEN_SLASH, TOKEN_MOD};
  return parse_binary_op(l, parse_unary, ops, 3);
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
ASTNode* parse_bitwise_and(Lexer *l) {
  TokenType ops[] = {TOKEN_AND};
  return parse_binary_op(l, parse_equality, ops, 1);
}
ASTNode* parse_bitwise_xor(Lexer *l) {
  TokenType ops[] = {TOKEN_XOR};
  return parse_binary_op(l, parse_bitwise_and, ops, 1);
}
ASTNode* parse_bitwise_or(Lexer *l) {
  TokenType ops[] = {TOKEN_OR};
  return parse_binary_op(l, parse_bitwise_xor, ops, 1);
}
ASTNode* parse_logic_and(Lexer *l) {
  TokenType ops[] = {TOKEN_AND_AND};
  return parse_binary_op(l, parse_bitwise_or, ops, 1);
}
ASTNode* parse_logic_or(Lexer *l) {
  TokenType ops[] = {TOKEN_OR_OR};
  return parse_binary_op(l, parse_logic_and, ops, 1);
}

ASTNode* parse_assignment(Lexer *l) {
  ASTNode *lhs = parse_logic_or(l); // Start of precedence chain (was parse_bitwise)
  
  // Check for assignment operators
  if (current_token.type == TOKEN_ASSIGN || 
      current_token.type == TOKEN_PLUS_ASSIGN ||
      current_token.type == TOKEN_MINUS_ASSIGN ||
      current_token.type == TOKEN_STAR_ASSIGN ||
      current_token.type == TOKEN_SLASH_ASSIGN ||
      current_token.type == TOKEN_MOD_ASSIGN ||
      current_token.type == TOKEN_AND_ASSIGN ||
      current_token.type == TOKEN_OR_ASSIGN ||
      current_token.type == TOKEN_XOR_ASSIGN ||
      current_token.type == TOKEN_LSHIFT_ASSIGN ||
      current_token.type == TOKEN_RSHIFT_ASSIGN) {
          
      int op = current_token.type;
      eat(l, op);
      
      ASTNode *rhs = parse_assignment(l); // Right associative
      
      AssignNode *node = calloc(1, sizeof(AssignNode));
      node->base.type = NODE_ASSIGN;
      node->value = rhs;
      node->op = op;

      if (lhs->type == NODE_VAR_REF) {
          node->name = ((VarRefNode*)lhs)->name; // Steal name
          ((VarRefNode*)lhs)->name = NULL; // Prevent double free
          free(lhs);
      } else if (lhs->type == NODE_ARRAY_ACCESS) {
          node->name = ((ArrayAccessNode*)lhs)->name;
          node->index = ((ArrayAccessNode*)lhs)->index;
          ((ArrayAccessNode*)lhs)->name = NULL;
          ((ArrayAccessNode*)lhs)->index = NULL; // Prevent double free of index AST
          free(lhs);
      } else {
          parser_fail("Invalid l-value for assignment");
      }
      return (ASTNode*)node;
  }
  return lhs;
}

// Main Expression Entry Point
ASTNode* parse_expression(Lexer *l) {
  return parse_assignment(l);
}
