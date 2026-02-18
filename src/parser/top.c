#include "parser_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h> 

ASTNode* parse_extern(Parser *p) {
  eat(p, TOKEN_EXTERN);
  
  if (p->current_token.type == TOKEN_CLASS || p->current_token.type == TOKEN_STRUCT || p->current_token.type == TOKEN_UNION) {
      eat(p, p->current_token.type);
      if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected name for extern class/struct/union");
      char *name = parser_strdup(p, p->current_token.text);
      eat(p, TOKEN_IDENTIFIER);
      eat(p, TOKEN_SEMICOLON);
      
      register_typename(p, name, 0); 
      
      ClassNode *cn = parser_alloc(p, sizeof(ClassNode));
      cn->base.type = NODE_CLASS;
      cn->name = name;
      cn->is_extern = 1; 
      return (ASTNode*)cn;
  }
  
  VarType ret_type = parse_type(p);
  if (ret_type.base == TYPE_UNKNOWN) { parser_fail(p, "Expected return type for extern function"); }
  if (p->current_token.type != TOKEN_IDENTIFIER) { parser_fail(p, "Expected extern function name"); }
  char *name = p->current_token.text; p->current_token.text = NULL; eat(p, TOKEN_IDENTIFIER);
  name = parser_strdup(p, name);

  eat(p, TOKEN_LPAREN);
  Parameter *params_head = NULL; Parameter **curr_param = &params_head;
  int is_varargs = 0;
  if (p->current_token.type != TOKEN_RPAREN) {
    while (1) {
      if (p->current_token.type == TOKEN_ELLIPSIS) { eat(p, TOKEN_ELLIPSIS); is_varargs = 1; break; }
      VarType ptype = parse_type(p);
      if (ptype.base == TYPE_UNKNOWN) { parser_fail(p, "Expected parameter type"); }
      char *pname = NULL;
      if (p->current_token.type == TOKEN_IDENTIFIER) { pname = parser_strdup(p, p->current_token.text); p->current_token.text = NULL; eat(p, TOKEN_IDENTIFIER); }
      
      if (p->current_token.type == TOKEN_LBRACKET) {
          eat(p, TOKEN_LBRACKET);
          if (p->current_token.type != TOKEN_RBRACKET) {
              ASTNode *sz = parse_expression(p);
              (void)sz;
          }
          eat(p, TOKEN_RBRACKET);
          ptype.ptr_depth++;
      }
      
      Parameter *param = parser_alloc(p, sizeof(Parameter)); param->type = ptype; param->name = pname; *curr_param = param; curr_param = &param->next;
      if (p->current_token.type == TOKEN_COMMA) eat(p, TOKEN_COMMA); else break;
    }
  }
  eat(p, TOKEN_RPAREN); eat(p, TOKEN_SEMICOLON);
  FuncDefNode *node = parser_alloc(p, sizeof(FuncDefNode));
  node->base.type = NODE_FUNC_DEF; node->name = name; node->ret_type = ret_type;
  node->params = params_head; node->body = NULL; node->is_varargs = is_varargs;
  return (ASTNode*)node;
}

ASTNode* parse_enum(Parser *p) {
  eat(p, TOKEN_ENUM);
  if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected enum name");
  char *enum_name = parser_strdup(p, p->current_token.text);
  eat(p, TOKEN_IDENTIFIER);
  register_typename(p, enum_name, 1);

  eat(p, TOKEN_LBRACKET);
  
  EnumEntry *entries_head = NULL;
  EnumEntry **curr_entry = &entries_head;
  int current_val = 0;

  while (p->current_token.type != TOKEN_RBRACKET && p->current_token.type != TOKEN_EOF) {
      if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected enum member name");
      char *member_name = parser_strdup(p, p->current_token.text);
      eat(p, TOKEN_IDENTIFIER);
      
      if (p->current_token.type == TOKEN_ASSIGN) {
          eat(p, TOKEN_ASSIGN);
          int sign = 1;
          if (p->current_token.type == TOKEN_MINUS) { sign = -1; eat(p, TOKEN_MINUS); }
          if (p->current_token.type != TOKEN_NUMBER) parser_fail(p, "Expected integer value for enum member");
          current_val = p->current_token.int_val * sign;
          eat(p, TOKEN_NUMBER);
      }
      
      EnumEntry *entry = parser_alloc(p, sizeof(EnumEntry));
      entry->name = member_name;
      entry->value = current_val;
      entry->next = NULL;
      *curr_entry = entry;
      curr_entry = &entry->next;
      
      current_val++;
      
      if (p->current_token.type == TOKEN_COMMA) eat(p, TOKEN_COMMA);
      else if (p->current_token.type != TOKEN_RBRACKET) parser_fail(p, "Expected ',' or ']' in enum definition");
  }
  eat(p, TOKEN_RBRACKET);
  eat(p, TOKEN_SEMICOLON);

  EnumNode *en = parser_alloc(p, sizeof(EnumNode));
  en->base.type = NODE_ENUM;
  en->name = enum_name;
  en->entries = entries_head;
  return (ASTNode*)en;
}

// todo split this into modular
ASTNode* parse_class(Parser *p) {  
  int is_open = 0;
  if (p->current_token.type == TOKEN_OPEN) { is_open = 1; eat(p, TOKEN_OPEN); }
  else if (p->current_token.type == TOKEN_CLOSED) { is_open = 0; eat(p, TOKEN_CLOSED); }
  
  if (p->current_token.type == TOKEN_CLASS || p->current_token.type == TOKEN_STRUCT || p->current_token.type == TOKEN_UNION) {
      int is_union = (p->current_token.type == TOKEN_UNION);
      eat(p, p->current_token.type);
      if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected name after 'class', 'struct' or 'union'");
      char *class_name = parser_strdup(p, p->current_token.text);
      eat(p, TOKEN_IDENTIFIER);
      
      register_typename(p, class_name, 0); 
      
      char *parent_name = NULL;
      if (p->current_token.type == TOKEN_IS) {
          eat(p, TOKEN_IS);
          if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected parent class name after 'is'");
          parent_name = parser_strdup(p, p->current_token.text);
          eat(p, TOKEN_IDENTIFIER);
      }
      
      char **traits = NULL;
      int trait_count = 0;
      if (p->current_token.type == TOKEN_HAS) {
          eat(p, TOKEN_HAS);
          int cap = 4;
          traits = parser_alloc(p, sizeof(char*) * cap);
          do {
              if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected trait name after 'has'");
              if (trait_count >= cap) { 
                  cap *= 2; 
                  char **new_traits = parser_alloc(p, sizeof(char*)*cap);
                  memcpy(new_traits, traits, sizeof(char*)*trait_count);
                  traits = new_traits;
              }
              traits[trait_count++] = parser_strdup(p, p->current_token.text);
              eat(p, TOKEN_IDENTIFIER);
              if (p->current_token.type == TOKEN_COMMA) eat(p, TOKEN_COMMA);
              else break;
          } while(1);
      }
      
      eat(p, TOKEN_LBRACE);
      
      ASTNode *members_head = NULL;
      ASTNode **curr_member = &members_head;
      
      while (p->current_token.type != TOKEN_RBRACE && p->current_token.type != TOKEN_EOF) {
          int member_open = is_open;
          int line = p->current_token.line;
          int col = p->current_token.col;
          if (p->current_token.type == TOKEN_OPEN) { member_open = 1; eat(p, TOKEN_OPEN); }
          else if (p->current_token.type == TOKEN_CLOSED) { member_open = 0; eat(p, TOKEN_CLOSED); }
          
          if (p->current_token.type == TOKEN_FLUX) {
              eat(p, TOKEN_FLUX);
              VarType vt = parse_type(p);
              
              eat(p, TOKEN_LBRACE);
              ASTNode *body = parse_statements(p);
              eat(p, TOKEN_RBRACE);
              
              FuncDefNode *func = parser_alloc(p, sizeof(FuncDefNode));
              func->base.type = NODE_FUNC_DEF;
              func->base.line = line;
              func->base.col = col;
              func->name = parser_strdup(p, "iterate");
              func->ret_type = vt;
              func->params = NULL;
              func->body = body;
              func->is_open = member_open;
              func->class_name = parser_strdup(p, class_name);
              func->is_flux = 1;
              
              *curr_member = (ASTNode*)func;
              curr_member = &func->base.next;
              continue;
          }

          VarType vt = parse_type(p);
          if (vt.base != TYPE_UNKNOWN) {
              if (p->current_token.type == TOKEN_LPAREN) {
                  char *mem_name = NULL;
                  vt = parse_func_ptr_decl(p, vt, &mem_name);
                  
                  ASTNode *init = NULL;
                  if (p->current_token.type == TOKEN_ASSIGN) {
                      eat(p, TOKEN_ASSIGN);
                      init = parse_expression(p);
                  }
                  eat(p, TOKEN_SEMICOLON);
                  
                  VarDeclNode *var = parser_alloc(p, sizeof(VarDeclNode));
                  var->base.type = NODE_VAR_DECL;
                  var->base.line = line;
                  var->base.col = col;
                  var->name = mem_name;
                  var->var_type = vt;
                  var->initializer = init;
                  var->is_mutable = 1; 
                  var->is_open = member_open;
                  
                  *curr_member = (ASTNode*)var;
                  curr_member = &var->base.next;
                  continue;
              }

              if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected member name in class body");
              char *mem_name = parser_strdup(p, p->current_token.text);
              eat(p, TOKEN_IDENTIFIER);
              
              if (p->current_token.type == TOKEN_LPAREN) {
                  eat(p, TOKEN_LPAREN);
                  Parameter *params = NULL;
                  Parameter **curr_p = &params;
                  
                  if (p->current_token.type != TOKEN_RPAREN) {
                      while(1) {
                          VarType pt = parse_type(p);
                          if(pt.base == TYPE_UNKNOWN) parser_fail(p, "Expected parameter type in method declaration");
                          char *pname = parser_strdup(p, p->current_token.text);
                          eat(p, TOKEN_IDENTIFIER);
                          
                          if (p->current_token.type == TOKEN_LBRACKET) {
                              eat(p, TOKEN_LBRACKET);
                              if (p->current_token.type != TOKEN_RBRACKET) {
                                  ASTNode *sz = parse_expression(p);
                                  (void)sz;
                              }
                              eat(p, TOKEN_RBRACKET);
                              pt.ptr_depth++;
                          }
                          
                          Parameter *pm = parser_alloc(p, sizeof(Parameter));
                          pm->type = pt; pm->name = pname;
                          *curr_p = pm; curr_p = &pm->next;
                          if (p->current_token.type == TOKEN_COMMA) eat(p, TOKEN_COMMA); else break;
                      }
                  }
                  eat(p, TOKEN_RPAREN);
                  eat(p, TOKEN_LBRACE);
                  ASTNode *body = parse_statements(p);
                  eat(p, TOKEN_RBRACE);
                  
                  FuncDefNode *func = parser_alloc(p, sizeof(FuncDefNode));
                  func->base.type = NODE_FUNC_DEF;
                  func->base.line = line;
                  func->base.col = col;
                  func->name = mem_name;
                  func->ret_type = vt;
                  func->params = params;
                  func->body = body;
                  func->is_open = member_open;
                  func->class_name = parser_strdup(p, class_name); 
                  
                  *curr_member = (ASTNode*)func;
                  curr_member = &func->base.next;
              } else {
                  int is_array = 0;
                  ASTNode *array_size = NULL;
                  ASTNode **curr_sz = &array_size;

                  while (p->current_token.type == TOKEN_LBRACKET) {
                      is_array = 1;
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
                  eat(p, TOKEN_SEMICOLON);
                  
                  VarDeclNode *var = parser_alloc(p, sizeof(VarDeclNode));
                  var->base.type = NODE_VAR_DECL;
                  var->base.line = line;
                  var->base.col = col;
                  var->name = mem_name;
                  var->var_type = vt;
                  var->initializer = init;
                  var->is_mutable = 1; 
                  var->is_open = member_open;
                  var->is_array = is_array;
                  var->array_size = array_size; 
                  
                  *curr_member = (ASTNode*)var;
                  curr_member = &var->base.next;
              }
          } else {
              parser_fail(p, "Unexpected token in class body. Expected member declaration or '}'.");
          }
      }
      eat(p, TOKEN_RBRACE);
      
      ClassNode *cls = parser_alloc(p, sizeof(ClassNode));
      cls->base.type = NODE_CLASS;
      cls->name = class_name;
      cls->parent_name = parent_name;
      cls->traits.names = traits;
      cls->traits.count = trait_count;
      cls->members = members_head;
      cls->is_open = is_open;
      cls->is_union = is_union;
      
      return (ASTNode*)cls;
  }
  return NULL;
}

ASTNode* parse_top_level(Parser *p) { 
  if (p->current_token.type == TOKEN_SEMICOLON) {
      eat(p, TOKEN_SEMICOLON);
      return NULL;
  }

  if (p->current_token.type == TOKEN_NAMESPACE) {
      eat(p, TOKEN_NAMESPACE);
      if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected namespace name");
      char *ns_name = parser_strdup(p, p->current_token.text);
      eat(p, TOKEN_IDENTIFIER);

      eat(p, TOKEN_LBRACE);
      
      ASTNode *body_head = NULL;
      ASTNode **body_curr = &body_head;
      
      while(p->current_token.type != TOKEN_RBRACE && p->current_token.type != TOKEN_EOF) {
          ASTNode *n = parse_top_level(p);
          if (n) {
              *body_curr = n;
              while (*body_curr) body_curr = &(*body_curr)->next;
          }
      }
      eat(p, TOKEN_RBRACE);
      
      NamespaceNode *ns = parser_alloc(p, sizeof(NamespaceNode));
      ns->base.type = NODE_NAMESPACE;
      ns->name = ns_name;
      ns->body = body_head;
      return (ASTNode*)ns;
  }

  if (p->current_token.type == TOKEN_DEFINE) return parse_define(p);
  if (p->current_token.type == TOKEN_TYPEDEF) return parse_typedef(p);
  if (p->current_token.type == TOKEN_ENUM) return parse_enum(p);
  
  if (p->current_token.type == TOKEN_CLASS || 
      p->current_token.type == TOKEN_STRUCT || 
      p->current_token.type == TOKEN_UNION || 
      (p->current_token.type == TOKEN_OPEN) || 
      (p->current_token.type == TOKEN_CLOSED)) {
    return parse_class(p);
  }

  if (p->current_token.type == TOKEN_LINK) return parse_link(p);
  if (p->current_token.type == TOKEN_IMPORT) return parse_import(p);
  if (p->current_token.type == TOKEN_EXTERN) return parse_extern(p);

  if (p->current_token.type == TOKEN_KW_MUT || p->current_token.type == TOKEN_KW_IMUT) {
    return parse_var_decl_internal(p);
  }

  int line = p->current_token.line;
  int col = p->current_token.col;

  int is_flux = 0;
  if (p->current_token.type == TOKEN_FLUX) {
      is_flux = 1;
      eat(p, TOKEN_FLUX);
  }

  VarType vtype = parse_type(p);
  if (vtype.base == TYPE_UNKNOWN) {
      return parse_single_statement_or_block(p);
  }

  if (p->current_token.type == TOKEN_LPAREN) {
      char *name = NULL;
      vtype = parse_func_ptr_decl(p, vtype, &name);
      
      ASTNode *init = NULL;
      if (p->current_token.type == TOKEN_ASSIGN) {
          eat(p, TOKEN_ASSIGN);
          init = parse_expression(p);
      }
      eat(p, TOKEN_SEMICOLON);
      
      VarDeclNode *node = parser_alloc(p, sizeof(VarDeclNode));
      node->base.type = NODE_VAR_DECL;
      node->var_type = vtype;
      node->name = name;
      node->initializer = init;
      node->is_mutable = 1; 
      node->base.line = line; node->base.col = col;
      return (ASTNode*)node;
  }

  if (p->current_token.type != TOKEN_IDENTIFIER) { parser_fail(p, "Expected identifier definition after type"); }
  char *name = parser_strdup(p, p->current_token.text); 
  p->current_token.text = NULL; eat(p, TOKEN_IDENTIFIER);
  
  if (p->current_token.type == TOKEN_LPAREN) {
    eat(p, TOKEN_LPAREN);
    Parameter *params_head = NULL; Parameter **curr_param = &params_head;
    if (p->current_token.type != TOKEN_RPAREN) {
      while (1) {
        VarType ptype = parse_type(p);
        if (ptype.base == TYPE_UNKNOWN) parser_fail(p, "Expected parameter type in function definition");
        char *pname = parser_strdup(p, p->current_token.text); 
        p->current_token.text = NULL; eat(p, TOKEN_IDENTIFIER);
        
        if (p->current_token.type == TOKEN_LBRACKET) {
            eat(p, TOKEN_LBRACKET);
            if (p->current_token.type != TOKEN_RBRACKET) {
                ASTNode *sz = parse_expression(p);
                (void)sz;
            }
            eat(p, TOKEN_RBRACKET);
            ptype.ptr_depth++;
        }
        
        Parameter *pm = parser_alloc(p, sizeof(Parameter)); pm->type = ptype; pm->name = pname; *curr_param = pm; curr_param = &pm->next;
        if (p->current_token.type == TOKEN_COMMA) eat(p, TOKEN_COMMA); else break;
      }
    }
    eat(p, TOKEN_RPAREN); eat(p, TOKEN_LBRACE);
    ASTNode *body = parse_statements(p); eat(p, TOKEN_RBRACE);
    FuncDefNode *node = parser_alloc(p, sizeof(FuncDefNode));
    node->base.type = NODE_FUNC_DEF; node->name = name; node->ret_type = vtype; node->params = params_head; node->body = body;
    node->is_flux = is_flux; 
    node->base.line = line; node->base.col = col;
    return (ASTNode*)node;
  } else {
    char *name_val = name;
    int is_array = 0;
    ASTNode *array_size = NULL;
    ASTNode **curr_sz = &array_size;
    
    while (p->current_token.type == TOKEN_LBRACKET) {
      is_array = 1; eat(p, TOKEN_LBRACKET);
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
    if (p->current_token.type == TOKEN_ASSIGN) { eat(p, TOKEN_ASSIGN); init = parse_expression(p); } 
    else { if (vtype.base == TYPE_AUTO) { parser_fail(p, "'let' variable declaration must have an initializer"); } }
    eat(p, TOKEN_SEMICOLON);
    VarDeclNode *node = parser_alloc(p, sizeof(VarDeclNode));
    node->base.type = NODE_VAR_DECL; node->var_type = vtype; node->name = name_val;
    node->initializer = init; node->is_mutable = 1; 
    node->is_array = is_array; node->array_size = array_size; 
    node->base.line = line; node->base.col = col;
    return (ASTNode*)node;
  }
}
