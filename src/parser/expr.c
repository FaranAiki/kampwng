#include "parser_internal.h"
#include <string.h>
#include <stdlib.h>

static void set_loc(ASTNode *n, int line, int col) {
    if (n) { n->line = line; n->col = col; }
}

ASTNode* parse_unary(Parser *p);

ASTNode* parse_call(Parser *p, char *name) {
  eat(p, TOKEN_LPAREN);
  ASTNode *args_head = NULL;
  ASTNode **curr_arg = &args_head;
  if (p->current_token.type != TOKEN_RPAREN) {
    *curr_arg = parse_expression(p);
    curr_arg = &(*curr_arg)->next;
    while (p->current_token.type == TOKEN_COMMA) {
      eat(p, TOKEN_COMMA);
      *curr_arg = parse_expression(p);
      curr_arg = &(*curr_arg)->next;
    }
  }
  eat(p, TOKEN_RPAREN);
  CallNode *node = parser_alloc(p, sizeof(CallNode));
  node->base.type = NODE_CALL;
  node->name = name;
  node->args = args_head;
  return (ASTNode*)node;
}

ASTNode* parse_postfix(Parser *p, ASTNode *node) {
    while (1) {
        int line = p->current_token.line;
        int col = p->current_token.col;

        if (p->current_token.type == TOKEN_DOT) {
            eat(p, TOKEN_DOT);
            if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected member name after '.'");
            char *member = p->current_token.text;
            p->current_token.text = NULL;
            eat(p, TOKEN_IDENTIFIER);
            
            if (p->current_token.type == TOKEN_LPAREN) {
                eat(p, TOKEN_LPAREN);
                ASTNode *args_head = NULL;
                ASTNode **curr_arg = &args_head;
                if (p->current_token.type != TOKEN_RPAREN) {
                    *curr_arg = parse_expression(p);
                    curr_arg = &(*curr_arg)->next;
                    while (p->current_token.type == TOKEN_COMMA) {
                        eat(p, TOKEN_COMMA);
                        *curr_arg = parse_expression(p);
                        curr_arg = &(*curr_arg)->next;
                    }
                }
                eat(p, TOKEN_RPAREN);
                
                MethodCallNode *mc = parser_alloc(p, sizeof(MethodCallNode));
                mc->base.type = NODE_METHOD_CALL;
                mc->object = node;
                mc->method_name = member;
                mc->args = args_head;
                node = (ASTNode*)mc;
            } else {
                MemberAccessNode *ma = parser_alloc(p, sizeof(MemberAccessNode));
                ma->base.type = NODE_MEMBER_ACCESS;
                ma->object = node;
                ma->member_name = member;
                node = (ASTNode*)ma;
            }
            set_loc(node, line, col);
        } 
        else if (p->current_token.type == TOKEN_LBRACKET) {
            eat(p, TOKEN_LBRACKET);
            
            if (p->current_token.type == TOKEN_IDENTIFIER && is_typename(p, p->current_token.text)) {
                char *trait_name = parser_strdup(p, p->current_token.text);
                eat(p, TOKEN_IDENTIFIER);
                eat(p, TOKEN_RBRACKET);
                
                TraitAccessNode *ta = parser_alloc(p, sizeof(TraitAccessNode));
                ta->base.type = NODE_TRAIT_ACCESS;
                ta->object = node;
                ta->trait_name = trait_name;
                node = (ASTNode*)ta;
            } else {
                ASTNode *index = parse_expression(p);
                eat(p, TOKEN_RBRACKET);
                
                ArrayAccessNode *aa = parser_alloc(p, sizeof(ArrayAccessNode));
                aa->base.type = NODE_ARRAY_ACCESS;
                aa->target = node; 
                aa->index = index;
                node = (ASTNode*)aa;
            }
            set_loc(node, line, col);
        }
        else if (p->current_token.type == TOKEN_INCREMENT || p->current_token.type == TOKEN_DECREMENT) {
            int op = p->current_token.type;
            eat(p, op);
            IncDecNode *id = parser_alloc(p, sizeof(IncDecNode));
            id->base.type = NODE_INC_DEC;
            id->target = node;
            id->is_prefix = 0;
            id->op = op;
            node = (ASTNode*)id;
            set_loc(node, line, col);
        }
        else if (p->current_token.type == TOKEN_AS) {
            eat(p, TOKEN_AS);
            VarType t = parse_type(p);
            
            CastNode *cn = parser_alloc(p, sizeof(CastNode));
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

ASTNode* parse_factor(Parser *p) {
  ASTNode *node = NULL;
  int line = p->current_token.line;
  int col = p->current_token.col;

  if (p->current_token.type == TOKEN_TYPEOF) {
      eat(p, TOKEN_TYPEOF);
      ASTNode *expr;
      if (p->current_token.type == TOKEN_LPAREN) {
          eat(p, TOKEN_LPAREN);
          expr = parse_expression(p);
          eat(p, TOKEN_RPAREN);
      } else {
          expr = parse_unary(p); 
      }
      UnaryOpNode *u = parser_alloc(p, sizeof(UnaryOpNode));
      u->base.type = NODE_TYPEOF;
      u->operand = expr;
      node = (ASTNode*)u;
      set_loc(node, line, col);
  }
  else if (p->current_token.type == TOKEN_HASMETHOD) {
      eat(p, TOKEN_HASMETHOD);
      ASTNode *expr;
      if (p->current_token.type == TOKEN_LPAREN) {
          eat(p, TOKEN_LPAREN);
          expr = parse_expression(p);
          eat(p, TOKEN_RPAREN);
      } else {
          expr = parse_unary(p);
      }
      UnaryOpNode *u = parser_alloc(p, sizeof(UnaryOpNode));
      u->base.type = NODE_HAS_METHOD;
      u->operand = expr;
      node = (ASTNode*)u;
      set_loc(node, line, col);
  }
  else if (p->current_token.type == TOKEN_HASATTRIBUTE) {
      eat(p, TOKEN_HASATTRIBUTE);
      ASTNode *expr;
      if (p->current_token.type == TOKEN_LPAREN) {
          eat(p, TOKEN_LPAREN);
          expr = parse_expression(p);
          eat(p, TOKEN_RPAREN);
      } else {
          expr = parse_unary(p);
      }
      UnaryOpNode *u = parser_alloc(p, sizeof(UnaryOpNode));
      u->base.type = NODE_HAS_ATTRIBUTE;
      u->operand = expr;
      node = (ASTNode*)u;
      set_loc(node, line, col);
  }
  else if (p->current_token.type == TOKEN_LBRACKET) {
    eat(p, TOKEN_LBRACKET);
    ASTNode *elems_head = NULL;
    ASTNode **curr_elem = &elems_head;
    if (p->current_token.type != TOKEN_RBRACKET) {
      *curr_elem = parse_expression(p);
      curr_elem = &(*curr_elem)->next;
      while (p->current_token.type == TOKEN_COMMA) {
        eat(p, TOKEN_COMMA);
        *curr_elem = parse_expression(p);
        curr_elem = &(*curr_elem)->next;
      }
    }
    eat(p, TOKEN_RBRACKET);
    ArrayLitNode *an = parser_alloc(p, sizeof(ArrayLitNode));
    an->base.type = NODE_ARRAY_LIT;
    an->elements = elems_head;
    node = (ASTNode*)an;
    set_loc(node, line, col);
  }
  else if (p->current_token.type == TOKEN_NUMBER || 
           p->current_token.type == TOKEN_UINT_LIT ||
           p->current_token.type == TOKEN_LONG_LIT ||
           p->current_token.type == TOKEN_ULONG_LIT ||
           p->current_token.type == TOKEN_LONG_LONG_LIT ||
           p->current_token.type == TOKEN_ULONG_LONG_LIT) {
    LiteralNode *ln = parser_alloc(p, sizeof(LiteralNode));
    ln->base.type = NODE_LITERAL;
    
    if (p->current_token.type == TOKEN_UINT_LIT) { ln->var_type.base = TYPE_INT; ln->var_type.is_unsigned = 1; }
    else if (p->current_token.type == TOKEN_LONG_LIT) { ln->var_type.base = TYPE_LONG; }
    else if (p->current_token.type == TOKEN_ULONG_LIT) { ln->var_type.base = TYPE_LONG; ln->var_type.is_unsigned = 1; }
    else if (p->current_token.type == TOKEN_LONG_LONG_LIT) { ln->var_type.base = TYPE_LONG_LONG; }
    else if (p->current_token.type == TOKEN_ULONG_LONG_LIT) { ln->var_type.base = TYPE_LONG_LONG; ln->var_type.is_unsigned = 1; }
    else { ln->var_type.base = TYPE_INT; }

    ln->val.long_val = p->current_token.long_val;
    eat(p, p->current_token.type);
    node = (ASTNode*)ln;
    set_loc(node, line, col);
  }
  else if (p->current_token.type == TOKEN_FLOAT || p->current_token.type == TOKEN_LONG_DOUBLE_LIT) {
    LiteralNode *ln = parser_alloc(p, sizeof(LiteralNode));
    ln->base.type = NODE_LITERAL;
    if (p->current_token.type == TOKEN_LONG_DOUBLE_LIT) ln->var_type.base = TYPE_LONG_DOUBLE;
    else ln->var_type.base = TYPE_DOUBLE;
    ln->val.double_val = p->current_token.double_val;
    eat(p, p->current_token.type);
    node = (ASTNode*)ln;
    set_loc(node, line, col);
  }
  else if (p->current_token.type == TOKEN_CHAR_LIT) {
    LiteralNode *ln = parser_alloc(p, sizeof(LiteralNode));
    ln->base.type = NODE_LITERAL;
    ln->var_type.base = TYPE_CHAR;
    ln->val.long_val = p->current_token.int_val;
    eat(p, TOKEN_CHAR_LIT);
    node = (ASTNode*)ln;
    set_loc(node, line, col);
  }
  else if (p->current_token.type == TOKEN_STRING) {
    LiteralNode *ln = parser_alloc(p, sizeof(LiteralNode));
    ln->base.type = NODE_LITERAL;
    ln->var_type.base = TYPE_STRING;
    ln->val.str_val = parser_strdup(p, p->current_token.text);
    p->current_token.text = NULL; 
    eat(p, TOKEN_STRING);
    node = (ASTNode*)ln;
    set_loc(node, line, col);
  }
  else if (p->current_token.type == TOKEN_C_STRING) {
    LiteralNode *ln = parser_alloc(p, sizeof(LiteralNode));
    ln->base.type = NODE_LITERAL;
    ln->var_type.base = TYPE_CHAR;
    ln->var_type.ptr_depth = 1; 
    ln->val.str_val = parser_strdup(p, p->current_token.text);
    p->current_token.text = NULL; 
    eat(p, TOKEN_C_STRING);
    node = (ASTNode*)ln;
    set_loc(node, line, col);
  }
  else if (p->current_token.type == TOKEN_TRUE || p->current_token.type == TOKEN_FALSE) {
    LiteralNode *ln = parser_alloc(p, sizeof(LiteralNode));
    ln->base.type = NODE_LITERAL;
    ln->var_type.base = TYPE_BOOL;
    ln->val.long_val = (p->current_token.type == TOKEN_TRUE) ? 1 : 0;
    eat(p, p->current_token.type);
    node = (ASTNode*)ln;
    set_loc(node, line, col);
  }
  else if (p->current_token.type == TOKEN_IDENTIFIER) {
    char *name = parser_strdup(p, p->current_token.text);
    p->current_token.text = NULL;
    eat(p, TOKEN_IDENTIFIER);
    
    if (p->current_token.type == TOKEN_LPAREN) {
      node = parse_call(p, name);
      set_loc(node, line, col);
    } 
    else {
        VarRefNode *vn = parser_alloc(p, sizeof(VarRefNode));
        vn->base.type = NODE_VAR_REF;
        vn->name = name;
        node = (ASTNode*)vn;
        set_loc(node, line, col);
    }
  }
  else {
    char msg[128];
    const char *tok = p->current_token.text ? p->current_token.text : token_type_to_string(p->current_token.type);
    snprintf(msg, sizeof(msg), "Unexpected token in expression: '%s'", tok);
    parser_fail(p, msg);
    return NULL; 
  }
  
  return parse_postfix(p, node);
}

ASTNode* parse_unary(Parser *p) {
  int line = p->current_token.line;
  int col = p->current_token.col;
  
  if (p->current_token.type == TOKEN_INCREMENT || p->current_token.type == TOKEN_DECREMENT) {
      int op = p->current_token.type;
      eat(p, op);
      ASTNode *operand = parse_unary(p);
      IncDecNode *node = parser_alloc(p, sizeof(IncDecNode));
      node->base.type = NODE_INC_DEC;
      node->target = operand;
      node->is_prefix = 1;
      node->op = op;
      set_loc((ASTNode*)node, line, col);
      return (ASTNode*)node;
  }
  
  if (p->current_token.type == TOKEN_NOT || p->current_token.type == TOKEN_MINUS || 
      p->current_token.type == TOKEN_BIT_NOT || p->current_token.type == TOKEN_STAR || 
      p->current_token.type == TOKEN_AND) {
    int op = p->current_token.type;
    eat(p, op);
    ASTNode *operand = parse_unary(p);
    UnaryOpNode *node = parser_alloc(p, sizeof(UnaryOpNode));
    node->base.type = NODE_UNARY_OP;
    node->op = op;
    node->operand = operand;
    set_loc((ASTNode*)node, line, col);
    return (ASTNode*)node;
  }

  if (p->current_token.type == TOKEN_LPAREN) {
      eat(p, TOKEN_LPAREN);
      ASTNode *expr = parse_expression(p);
      eat(p, TOKEN_RPAREN);
      return parse_postfix(p, expr);
  }

  return parse_factor(p);
}

static ASTNode* parse_binary_op(Parser *p, ASTNode* (*sub_parser)(Parser*), TokenType* ops, int num_ops) {
  ASTNode *left = sub_parser(p);
  while (1) {
    int found = 0;
    int line = p->current_token.line;
    int col = p->current_token.col;
    for (int i = 0; i < num_ops; i++) {
      if (p->current_token.type == ops[i]) {
        found = 1;
        TokenType op = p->current_token.type;
        eat(p, op);
        ASTNode *right = sub_parser(p);
        BinaryOpNode *node = parser_alloc(p, sizeof(BinaryOpNode));
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

ASTNode* parse_term(Parser *p) {
  TokenType ops[] = {TOKEN_STAR, TOKEN_SLASH, TOKEN_MOD};
  return parse_binary_op(p, parse_unary, ops, 3);
}
ASTNode* parse_additive(Parser *p) {
  TokenType ops[] = {TOKEN_PLUS, TOKEN_MINUS};
  return parse_binary_op(p, parse_term, ops, 2);
}
ASTNode* parse_shift(Parser *p) {
  TokenType ops[] = {TOKEN_LSHIFT, TOKEN_RSHIFT};
  return parse_binary_op(p, parse_additive, ops, 2);
}
ASTNode* parse_relational(Parser *p) {
  TokenType ops[] = {TOKEN_LT, TOKEN_GT, TOKEN_LTE, TOKEN_GTE};
  return parse_binary_op(p, parse_shift, ops, 4);
}
ASTNode* parse_equality(Parser *p) {
  TokenType ops[] = {TOKEN_EQ, TOKEN_NEQ};
  return parse_binary_op(p, parse_relational, ops, 2);
}
ASTNode* parse_bitwise_and(Parser *p) {
  TokenType ops[] = {TOKEN_AND};
  return parse_binary_op(p, parse_equality, ops, 1);
}
ASTNode* parse_bitwise_xor(Parser *p) {
  TokenType ops[] = {TOKEN_XOR};
  return parse_binary_op(p, parse_bitwise_and, ops, 1);
}
ASTNode* parse_bitwise_or(Parser *p) {
  TokenType ops[] = {TOKEN_OR};
  return parse_binary_op(p, parse_bitwise_xor, ops, 1);
}
ASTNode* parse_logic_and(Parser *p) {
  TokenType ops[] = {TOKEN_AND_AND};
  return parse_binary_op(p, parse_bitwise_or, ops, 1);
}
ASTNode* parse_logic_or(Parser *p) {
  TokenType ops[] = {TOKEN_OR_OR};
  return parse_binary_op(p, parse_logic_and, ops, 1);
}

ASTNode* parse_assignment(Parser *p) {
  ASTNode *lhs = parse_logic_or(p); 
  
  if (p->current_token.type == TOKEN_ASSIGN || 
      p->current_token.type == TOKEN_PLUS_ASSIGN ||
      p->current_token.type == TOKEN_MINUS_ASSIGN ||
      p->current_token.type == TOKEN_STAR_ASSIGN ||
      p->current_token.type == TOKEN_SLASH_ASSIGN ||
      p->current_token.type == TOKEN_MOD_ASSIGN ||
      p->current_token.type == TOKEN_AND_ASSIGN ||
      p->current_token.type == TOKEN_OR_ASSIGN ||
      p->current_token.type == TOKEN_XOR_ASSIGN ||
      p->current_token.type == TOKEN_LSHIFT_ASSIGN ||
      p->current_token.type == TOKEN_RSHIFT_ASSIGN) {
          
      int line = p->current_token.line;
      int col = p->current_token.col;
      int op = p->current_token.type;
      eat(p, op);
      
      ASTNode *rhs = parse_assignment(p); 
      
      AssignNode *node = parser_alloc(p, sizeof(AssignNode));
      node->base.type = NODE_ASSIGN;
      node->value = rhs;
      node->op = op;

      if (lhs->type == NODE_VAR_REF) {
          node->name = ((VarRefNode*)lhs)->name; 
          ((VarRefNode*)lhs)->name = NULL; 
          // No free
      } else {
          node->target = lhs; 
      }
      set_loc((ASTNode*)node, line, col);
      return (ASTNode*)node;
  }
  return lhs;
}

ASTNode* parse_expression(Parser *p) {
  return parse_assignment(p);
}
