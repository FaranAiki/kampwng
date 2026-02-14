#include "parser_internal.h"
#include <string.h>
#include <stdlib.h>

// Forward declarations
int is_typename(const char *name); // from core.c
ASTNode* parse_unary(Lexer *l);

// Local helper to set location
static void set_loc(ASTNode *n, int line, int col) {
    if (n) { n->line = line; n->col = col; }
}

ASTNode* parse_call(Lexer *l, char *name) {
  int line = current_token.line;
  int col = current_token.col;
  
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

// Parses postfix operations: [], ., ++, --, as
ASTNode* parse_postfix(Lexer *l, ASTNode *node) {
    while (1) {
        int line = current_token.line;
        int col = current_token.col;

        if (current_token.type == TOKEN_DOT) {
            eat(l, TOKEN_DOT);
            if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected member name after '.'");
            char *member = current_token.text;
            current_token.text = NULL;
            eat(l, TOKEN_IDENTIFIER);
            
            // Check for method call immediately: .method(
            if (current_token.type == TOKEN_LPAREN) {
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
                
                MethodCallNode *mc = calloc(1, sizeof(MethodCallNode));
                mc->base.type = NODE_METHOD_CALL;
                mc->object = node;
                mc->method_name = member;
                mc->args = args_head;
                node = (ASTNode*)mc;
            } else {
                MemberAccessNode *ma = calloc(1, sizeof(MemberAccessNode));
                ma->base.type = NODE_MEMBER_ACCESS;
                ma->object = node;
                ma->member_name = member;
                node = (ASTNode*)ma;
            }
            set_loc(node, line, col);
        } 
        else if (current_token.type == TOKEN_LBRACKET) {
            eat(l, TOKEN_LBRACKET);
            
            // Check for Trait Access: [ClassName]
            if (current_token.type == TOKEN_IDENTIFIER && is_typename(current_token.text)) {
                char *trait_name = strdup(current_token.text);
                eat(l, TOKEN_IDENTIFIER);
                eat(l, TOKEN_RBRACKET);
                
                TraitAccessNode *ta = calloc(1, sizeof(TraitAccessNode));
                ta->base.type = NODE_TRAIT_ACCESS;
                ta->object = node;
                ta->trait_name = trait_name;
                node = (ASTNode*)ta;
            } else {
                // Generic Array Access
                ASTNode *index = parse_expression(l);
                eat(l, TOKEN_RBRACKET);
                
                ArrayAccessNode *aa = calloc(1, sizeof(ArrayAccessNode));
                aa->base.type = NODE_ARRAY_ACCESS;
                aa->target = node; // Generic target (allows nesting)
                aa->index = index;
                node = (ASTNode*)aa;
            }
            set_loc(node, line, col);
        }
        else if (current_token.type == TOKEN_INCREMENT || current_token.type == TOKEN_DECREMENT) {
            int op = current_token.type;
            eat(l, op);
            IncDecNode *id = calloc(1, sizeof(IncDecNode));
            id->base.type = NODE_INC_DEC;
            id->target = node;
            id->is_prefix = 0;
            id->op = op;
            node = (ASTNode*)id;
            set_loc(node, line, col);
        }
        else if (current_token.type == TOKEN_AS) {
            // New "as" casting syntax
            eat(l, TOKEN_AS);
            VarType t = parse_type(l);
            
            CastNode *cn = calloc(1, sizeof(CastNode));
            cn->base.type = NODE_CAST;
            cn->operand = node;
            cn->var_type = t;
            node = (ASTNode*)cn;
            set_loc(node, line, col);
        }
        else {
            break;
        }
    }
    return node;
}

ASTNode* parse_factor(Lexer *l) {
  ASTNode *node = NULL;
  int line = current_token.line;
  int col = current_token.col;

  if (current_token.type == TOKEN_TYPEOF) {
      eat(l, TOKEN_TYPEOF);
      ASTNode *expr;
      // Allow optional parenthesis
      if (current_token.type == TOKEN_LPAREN) {
          eat(l, TOKEN_LPAREN);
          expr = parse_expression(l);
          eat(l, TOKEN_RPAREN);
      } else {
          expr = parse_unary(l); // Parse unary to bind tightly to next factor
      }
      UnaryOpNode *u = calloc(1, sizeof(UnaryOpNode));
      u->base.type = NODE_TYPEOF;
      u->operand = expr;
      node = (ASTNode*)u;
      set_loc(node, line, col);
  }
  else if (current_token.type == TOKEN_HASMETHOD) {
      eat(l, TOKEN_HASMETHOD);
      ASTNode *expr;
      if (current_token.type == TOKEN_LPAREN) {
          eat(l, TOKEN_LPAREN);
          expr = parse_expression(l);
          eat(l, TOKEN_RPAREN);
      } else {
          expr = parse_unary(l);
      }
      UnaryOpNode *u = calloc(1, sizeof(UnaryOpNode));
      u->base.type = NODE_HAS_METHOD;
      u->operand = expr;
      node = (ASTNode*)u;
      set_loc(node, line, col);
  }
  else if (current_token.type == TOKEN_HASATTRIBUTE) {
      eat(l, TOKEN_HASATTRIBUTE);
      ASTNode *expr;
      if (current_token.type == TOKEN_LPAREN) {
          eat(l, TOKEN_LPAREN);
          expr = parse_expression(l);
          eat(l, TOKEN_RPAREN);
      } else {
          expr = parse_unary(l);
      }
      UnaryOpNode *u = calloc(1, sizeof(UnaryOpNode));
      u->base.type = NODE_HAS_ATTRIBUTE;
      u->operand = expr;
      node = (ASTNode*)u;
      set_loc(node, line, col);
  }
  else if (current_token.type == TOKEN_LBRACKET) {
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
    ArrayLitNode *an = calloc(1, sizeof(ArrayLitNode));
    an->base.type = NODE_ARRAY_LIT;
    an->elements = elems_head;
    node = (ASTNode*)an;
    set_loc(node, line, col);
  }
  else if (current_token.type == TOKEN_NUMBER || 
           current_token.type == TOKEN_UINT_LIT ||
           current_token.type == TOKEN_LONG_LIT ||
           current_token.type == TOKEN_ULONG_LIT ||
           current_token.type == TOKEN_LONG_LONG_LIT ||
           current_token.type == TOKEN_ULONG_LONG_LIT) {
    LiteralNode *ln = calloc(1, sizeof(LiteralNode));
    ln->base.type = NODE_LITERAL;
    
    if (current_token.type == TOKEN_UINT_LIT) { ln->var_type.base = TYPE_INT; ln->var_type.is_unsigned = 1; }
    else if (current_token.type == TOKEN_LONG_LIT) { ln->var_type.base = TYPE_LONG; }
    else if (current_token.type == TOKEN_ULONG_LIT) { ln->var_type.base = TYPE_LONG; ln->var_type.is_unsigned = 1; }
    else if (current_token.type == TOKEN_LONG_LONG_LIT) { ln->var_type.base = TYPE_LONG_LONG; }
    else if (current_token.type == TOKEN_ULONG_LONG_LIT) { ln->var_type.base = TYPE_LONG_LONG; ln->var_type.is_unsigned = 1; }
    else { ln->var_type.base = TYPE_INT; }

    ln->val.long_val = current_token.long_val;
    eat(l, current_token.type);
    node = (ASTNode*)ln;
    set_loc(node, line, col);
  }
  else if (current_token.type == TOKEN_FLOAT || current_token.type == TOKEN_LONG_DOUBLE_LIT) {
    LiteralNode *ln = calloc(1, sizeof(LiteralNode));
    ln->base.type = NODE_LITERAL;
    if (current_token.type == TOKEN_LONG_DOUBLE_LIT) ln->var_type.base = TYPE_LONG_DOUBLE;
    else ln->var_type.base = TYPE_DOUBLE;
    ln->val.double_val = current_token.double_val;
    eat(l, current_token.type);
    node = (ASTNode*)ln;
    set_loc(node, line, col);
  }
  else if (current_token.type == TOKEN_CHAR_LIT) {
    LiteralNode *ln = calloc(1, sizeof(LiteralNode));
    ln->base.type = NODE_LITERAL;
    ln->var_type.base = TYPE_CHAR;
    ln->val.long_val = current_token.int_val;
    eat(l, TOKEN_CHAR_LIT);
    node = (ASTNode*)ln;
    set_loc(node, line, col);
  }
  else if (current_token.type == TOKEN_STRING) {
    LiteralNode *ln = calloc(1, sizeof(LiteralNode));
    ln->base.type = NODE_LITERAL;
    ln->var_type.base = TYPE_STRING;
    ln->val.str_val = current_token.text;
    current_token.text = NULL; 
    eat(l, TOKEN_STRING);
    node = (ASTNode*)ln;
    set_loc(node, line, col);
  }
  else if (current_token.type == TOKEN_C_STRING) {
    LiteralNode *ln = calloc(1, sizeof(LiteralNode));
    ln->base.type = NODE_LITERAL;
    ln->var_type.base = TYPE_CHAR;
    ln->var_type.ptr_depth = 1; // char*
    ln->val.str_val = current_token.text;
    current_token.text = NULL; 
    eat(l, TOKEN_C_STRING);
    node = (ASTNode*)ln;
    set_loc(node, line, col);
  }
  else if (current_token.type == TOKEN_TRUE || current_token.type == TOKEN_FALSE) {
    LiteralNode *ln = calloc(1, sizeof(LiteralNode));
    ln->base.type = NODE_LITERAL;
    ln->var_type.base = TYPE_BOOL;
    ln->val.long_val = (current_token.type == TOKEN_TRUE) ? 1 : 0;
    eat(l, current_token.type);
    node = (ASTNode*)ln;
    set_loc(node, line, col);
  }
  else if (current_token.type == TOKEN_IDENTIFIER) {
    char *name = current_token.text;
    current_token.text = NULL;
    eat(l, TOKEN_IDENTIFIER);
    
    if (current_token.type == TOKEN_LPAREN) {
      node = parse_call(l, name);
      set_loc(node, line, col);
    } 
    else {
        VarRefNode *vn = calloc(1, sizeof(VarRefNode));
        vn->base.type = NODE_VAR_REF;
        vn->name = name;
        node = (ASTNode*)vn;
        set_loc(node, line, col);
    }
  }
  else {
    char msg[128];
    const char *tok = current_token.text ? current_token.text : token_type_to_string(current_token.type);
    snprintf(msg, sizeof(msg), "Unexpected token in expression: '%s'", tok);
    parser_fail(l, msg);
    return NULL; 
  }
  
  return parse_postfix(l, node);
}

ASTNode* parse_unary(Lexer *l) {
  int line = current_token.line;
  int col = current_token.col;
  
  if (current_token.type == TOKEN_INCREMENT || current_token.type == TOKEN_DECREMENT) {
      int op = current_token.type;
      eat(l, op);
      ASTNode *operand = parse_unary(l);
      IncDecNode *node = calloc(1, sizeof(IncDecNode));
      node->base.type = NODE_INC_DEC;
      node->target = operand;
      node->is_prefix = 1;
      node->op = op;
      set_loc((ASTNode*)node, line, col);
      return (ASTNode*)node;
  }
  
  if (current_token.type == TOKEN_NOT || current_token.type == TOKEN_MINUS || 
      current_token.type == TOKEN_BIT_NOT || current_token.type == TOKEN_STAR || 
      current_token.type == TOKEN_AND) {
    int op = current_token.type;
    eat(l, op);
    ASTNode *operand = parse_unary(l);
    UnaryOpNode *node = calloc(1, sizeof(UnaryOpNode));
    node->base.type = NODE_UNARY_OP;
    node->op = op;
    node->operand = operand;
    set_loc((ASTNode*)node, line, col);
    return (ASTNode*)node;
  }

  // Handle Parenthesis: Grouping only. Casts are now handled via 'as'.
  if (current_token.type == TOKEN_LPAREN) {
      eat(l, TOKEN_LPAREN);
      ASTNode *expr = parse_expression(l);
      eat(l, TOKEN_RPAREN);
      // Apply postfix ops (e.g. (a + b).method() or (a+b) as int)
      return parse_postfix(l, expr);
  }

  return parse_factor(l);
}

// ... Binary Ops ...
ASTNode* parse_binary_op(Lexer *l, ASTNode* (*sub_parser)(Lexer*), TokenType* ops, int num_ops) {
  ASTNode *left = sub_parser(l);
  while (1) {
    int found = 0;
    int line = current_token.line;
    int col = current_token.col;
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
        set_loc((ASTNode*)node, line, col);
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
  ASTNode *lhs = parse_logic_or(l); 
  
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
          
      int line = current_token.line;
      int col = current_token.col;
      int op = current_token.type;
      eat(l, op);
      
      ASTNode *rhs = parse_assignment(l); 
      
      AssignNode *node = calloc(1, sizeof(AssignNode));
      node->base.type = NODE_ASSIGN;
      node->value = rhs;
      node->op = op;

      if (lhs->type == NODE_VAR_REF) {
          node->name = ((VarRefNode*)lhs)->name; 
          ((VarRefNode*)lhs)->name = NULL; 
          free(lhs);
      } else {
          // Generic target (MemberAccess, ArrayAccess, etc)
          node->target = lhs; 
      }
      set_loc((ASTNode*)node, line, col);
      return (ASTNode*)node;
  }
  return lhs;
}

ASTNode* parse_expression(Lexer *l) {
  return parse_assignment(l);
}
