#include "parser_internal.h"
#include <string.h>
#include <stdlib.h>

// Forward decl
void parser_advance(Lexer *l);
void register_typename(const char *name); // from core.c

ASTNode* parse_top_level(Lexer *l) {
  
  // 0. NAMESPACE
  if (current_token.type == TOKEN_NAMESPACE) {
      eat(l, TOKEN_NAMESPACE);
      if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected namespace name");
      char *ns_name = strdup(current_token.text);
      eat(l, TOKEN_IDENTIFIER);
      
      eat(l, TOKEN_LBRACE);
      
      ASTNode *body_head = NULL;
      ASTNode **body_curr = &body_head;
      
      while(current_token.type != TOKEN_RBRACE && current_token.type != TOKEN_EOF) {
          ASTNode *n = parse_top_level(l);
          if (n) {
              *body_curr = n;
              // Advance pointer to the end of the newly added chain
              while (*body_curr) body_curr = &(*body_curr)->next;
          }
      }
      eat(l, TOKEN_RBRACE);
      
      NamespaceNode *ns = calloc(1, sizeof(NamespaceNode));
      ns->base.type = NODE_NAMESPACE;
      ns->name = ns_name;
      ns->body = body_head;
      return (ASTNode*)ns;
  }

  // 0.1 DEFINE
  if (current_token.type == TOKEN_DEFINE) {
    eat(l, TOKEN_DEFINE);
    if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected macro name after 'define'");
    char *macro_name = strdup(current_token.text);
    eat(l, TOKEN_IDENTIFIER);
    
    char **params = NULL;
    int param_count = 0;
    if (current_token.type == TOKEN_LPAREN) {
        eat(l, TOKEN_LPAREN);
        int cap = 4;
        params = malloc(sizeof(char*) * cap);
        while(current_token.type != TOKEN_RPAREN) {
            if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected parameter name in define definition");
            if (param_count >= cap) { cap *= 2; params = realloc(params, sizeof(char*)*cap); }
            params[param_count++] = strdup(current_token.text);
            eat(l, TOKEN_IDENTIFIER);
            if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA);
            else if (current_token.type != TOKEN_RPAREN) parser_fail(l, "Expected ',' or ')' in macro parameters");
        }
        eat(l, TOKEN_RPAREN);
    }
    
    eat(l, TOKEN_AS);
    Token *body_tokens = malloc(sizeof(Token) * 32);
    int body_cap = 32;
    int body_len = 0;
    while (current_token.type != TOKEN_SEMICOLON && current_token.type != TOKEN_EOF) {
        if (body_len >= body_cap) { body_cap *= 2; body_tokens = realloc(body_tokens, sizeof(Token)*body_cap); }
        Token t = current_token;
        if (t.text) t.text = strdup(t.text);
        body_tokens[body_len++] = t;
        eat(l, current_token.type);
    }
    if (current_token.type == TOKEN_SEMICOLON) eat(l, TOKEN_SEMICOLON);
    register_macro(macro_name, params, param_count, body_tokens, body_len);
    free(macro_name);
    free(body_tokens);
    return NULL; 
  }

  // --- CLASS PARSING ---
  if (current_token.type == TOKEN_CLASS || 
      (current_token.type == TOKEN_OPEN) || 
      (current_token.type == TOKEN_CLOSED)) {
      
      int is_open = 0;
      if (current_token.type == TOKEN_OPEN) { is_open = 1; eat(l, TOKEN_OPEN); }
      else if (current_token.type == TOKEN_CLOSED) { is_open = 0; eat(l, TOKEN_CLOSED); }
      
      if (current_token.type == TOKEN_CLASS) {
          eat(l, TOKEN_CLASS);
          if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected class name after 'class'");
          char *class_name = strdup(current_token.text);
          eat(l, TOKEN_IDENTIFIER);
          
          register_typename(class_name);
          
          char *parent_name = NULL;
          if (current_token.type == TOKEN_IS) {
              eat(l, TOKEN_IS);
              if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected parent class name after 'is'");
              parent_name = strdup(current_token.text);
              eat(l, TOKEN_IDENTIFIER);
          }
          
          char **traits = NULL;
          int trait_count = 0;
          if (current_token.type == TOKEN_HAS) {
              eat(l, TOKEN_HAS);
              int cap = 4;
              traits = malloc(sizeof(char*) * cap);
              do {
                  if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected trait name after 'has'");
                  if (trait_count >= cap) { cap *= 2; traits = realloc(traits, sizeof(char*)*cap); }
                  traits[trait_count++] = strdup(current_token.text);
                  eat(l, TOKEN_IDENTIFIER);
                  if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA);
                  else break;
              } while(1);
          }
          
          eat(l, TOKEN_LBRACE);
          
          ASTNode *members_head = NULL;
          ASTNode **curr_member = &members_head;
          
          while (current_token.type != TOKEN_RBRACE && current_token.type != TOKEN_EOF) {
              int member_open = is_open;
              if (current_token.type == TOKEN_OPEN) { member_open = 1; eat(l, TOKEN_OPEN); }
              else if (current_token.type == TOKEN_CLOSED) { member_open = 0; eat(l, TOKEN_CLOSED); }
              
              VarType vt = parse_type(l);
              if (vt.base != TYPE_UNKNOWN) {
                  if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected member name in class body");
                  char *mem_name = strdup(current_token.text);
                  eat(l, TOKEN_IDENTIFIER);
                  
                  if (current_token.type == TOKEN_LPAREN) {
                      // Method
                      eat(l, TOKEN_LPAREN);
                      Parameter *params = NULL;
                      Parameter **curr_p = &params;
                      
                      if (current_token.type != TOKEN_RPAREN) {
                          while(1) {
                              VarType pt = parse_type(l);
                              if(pt.base == TYPE_UNKNOWN) parser_fail(l, "Expected parameter type in method declaration");
                              char *pname = strdup(current_token.text);
                              eat(l, TOKEN_IDENTIFIER);
                              
                              if (current_token.type == TOKEN_LBRACKET) {
                                  eat(l, TOKEN_LBRACKET);
                                  if (current_token.type != TOKEN_RBRACKET) {
                                      ASTNode *sz = parse_expression(l);
                                      // Ignore size for parameter types (decay to pointer)
                                      // if (sz && sz->type == NODE_LITERAL) pt.array_size = ((LiteralNode*)sz)->val.int_val;
                                      free_ast(sz);
                                  }
                                  eat(l, TOKEN_RBRACKET);
                                  pt.ptr_depth++;
                              }
                              
                              Parameter *p = calloc(1, sizeof(Parameter));
                              p->type = pt; p->name = pname;
                              *curr_p = p; curr_p = &p->next;
                              if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA); else break;
                          }
                      }
                      eat(l, TOKEN_RPAREN);
                      eat(l, TOKEN_LBRACE);
                      ASTNode *body = parse_statements(l);
                      eat(l, TOKEN_RBRACE);
                      
                      FuncDefNode *func = calloc(1, sizeof(FuncDefNode));
                      func->base.type = NODE_FUNC_DEF;
                      func->name = mem_name;
                      func->ret_type = vt;
                      func->params = params;
                      func->body = body;
                      func->is_open = member_open;
                      
                      *curr_member = (ASTNode*)func;
                      curr_member = &func->base.next;
                  } else {
                      // Field
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
                      
                      VarDeclNode *var = calloc(1, sizeof(VarDeclNode));
                      var->base.type = NODE_VAR_DECL;
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
                  parser_fail(l, "Unexpected token in class body. Expected member declaration or '}'.");
              }
          }
          eat(l, TOKEN_RBRACE);
          
          ClassNode *cls = calloc(1, sizeof(ClassNode));
          cls->base.type = NODE_CLASS;
          cls->name = class_name;
          cls->parent_name = parent_name;
          cls->traits.names = traits;
          cls->traits.count = trait_count;
          cls->members = members_head;
          cls->is_open = is_open;
          
          return (ASTNode*)cls;
      }
  }

  // 1. LINK
  if (current_token.type == TOKEN_LINK) {
    eat(l, TOKEN_LINK);
    char *lib_name = NULL;
    if (current_token.type == TOKEN_IDENTIFIER || current_token.type == TOKEN_STRING) {
      lib_name = current_token.text;
      current_token.text = NULL;
      if (current_token.type == TOKEN_IDENTIFIER) eat(l, TOKEN_IDENTIFIER);
      else eat(l, TOKEN_STRING);
    } else {
      parser_fail(l, "Expected library name (string or identifier) after 'link'");
    }
    if (current_token.type == TOKEN_SEMICOLON) eat(l, TOKEN_SEMICOLON);
    LinkNode *node = calloc(1, sizeof(LinkNode));
    node->base.type = NODE_LINK;
    node->lib_name = lib_name;
    return (ASTNode*)node;
  }

  // 2. IMPORT
  if (current_token.type == TOKEN_IMPORT) {
    eat(l, TOKEN_IMPORT);
    if (current_token.type != TOKEN_STRING) parser_fail(l, "Expected file path string after 'import'");
    char* fname = current_token.text;
    current_token.text = NULL;
    eat(l, TOKEN_STRING);
    eat(l, TOKEN_SEMICOLON);
    
    char* src = read_import_file(fname);
    if (!src) { 
        char msg[256];
        snprintf(msg, 256, "Could not open imported file: '%s'", fname);
        free(fname); 
        parser_fail(l, msg); 
    }
    free(fname);
    
    Token saved_token = current_token;
    current_token.text = NULL; current_token.type = TOKEN_UNKNOWN; 
    Lexer import_l; lexer_init(&import_l, src);
    ASTNode* imported_root = parse_program(&import_l);
    free(src);
    current_token = saved_token;
    return imported_root; 
  }
  
  // 3. EXTERN (FFI)
  if (current_token.type == TOKEN_EXTERN) {
    eat(l, TOKEN_EXTERN);
    VarType ret_type = parse_type(l);
    if (ret_type.base == TYPE_UNKNOWN) { parser_fail(l, "Expected return type for extern function"); }
    if (current_token.type != TOKEN_IDENTIFIER) { parser_fail(l, "Expected extern function name"); }
    char *name = current_token.text; current_token.text = NULL; eat(l, TOKEN_IDENTIFIER);
    eat(l, TOKEN_LPAREN);
    Parameter *params_head = NULL; Parameter **curr_param = &params_head;
    int is_varargs = 0;
    if (current_token.type != TOKEN_RPAREN) {
      while (1) {
        if (current_token.type == TOKEN_ELLIPSIS) { eat(l, TOKEN_ELLIPSIS); is_varargs = 1; break; }
        VarType ptype = parse_type(l);
        if (ptype.base == TYPE_UNKNOWN) { parser_fail(l, "Expected parameter type"); }
        char *pname = NULL;
        if (current_token.type == TOKEN_IDENTIFIER) { pname = current_token.text; current_token.text = NULL; eat(l, TOKEN_IDENTIFIER); }
        
        if (current_token.type == TOKEN_LBRACKET) {
            eat(l, TOKEN_LBRACKET);
            if (current_token.type != TOKEN_RBRACKET) {
                ASTNode *sz = parse_expression(l);
                // Ignore size for parameter types (decay to pointer)
                // if (sz && sz->type == NODE_LITERAL) ptype.array_size = ((LiteralNode*)sz)->val.int_val;
                free_ast(sz);
            }
            eat(l, TOKEN_RBRACKET);
            ptype.ptr_depth++;
        }
        
        Parameter *p = calloc(1, sizeof(Parameter)); p->type = ptype; p->name = pname; *curr_param = p; curr_param = &p->next;
        if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA); else break;
      }
    }
    eat(l, TOKEN_RPAREN); eat(l, TOKEN_SEMICOLON);
    FuncDefNode *node = calloc(1, sizeof(FuncDefNode));
    node->base.type = NODE_FUNC_DEF; node->name = name; node->ret_type = ret_type;
    node->params = params_head; node->body = NULL; node->is_varargs = is_varargs;
    return (ASTNode*)node;
  }

  if (current_token.type == TOKEN_KW_MUT || current_token.type == TOKEN_KW_IMUT) {
    return parse_var_decl_internal(l);
  }

  VarType vtype = parse_type(l);
  if (vtype.base == TYPE_UNKNOWN) {
      return parse_single_statement_or_block(l);
  }

  if (current_token.type != TOKEN_IDENTIFIER) { parser_fail(l, "Expected identifier definition after type"); }
  char *name = current_token.text; current_token.text = NULL; eat(l, TOKEN_IDENTIFIER);
  
  if (current_token.type == TOKEN_LPAREN) {
    eat(l, TOKEN_LPAREN);
    Parameter *params_head = NULL; Parameter **curr_param = &params_head;
    if (current_token.type != TOKEN_RPAREN) {
      while (1) {
        VarType ptype = parse_type(l);
        if (ptype.base == TYPE_UNKNOWN) parser_fail(l, "Expected parameter type in function definition");
        char *pname = current_token.text; current_token.text = NULL; eat(l, TOKEN_IDENTIFIER);
        
        if (current_token.type == TOKEN_LBRACKET) {
            eat(l, TOKEN_LBRACKET);
            if (current_token.type != TOKEN_RBRACKET) {
                ASTNode *sz = parse_expression(l);
                // Ignore size for parameter types (decay to pointer)
                // if (sz && sz->type == NODE_LITERAL) ptype.array_size = ((LiteralNode*)sz)->val.int_val;
                free_ast(sz);
            }
            eat(l, TOKEN_RBRACKET);
            ptype.ptr_depth++;
        }
        
        Parameter *p = calloc(1, sizeof(Parameter)); p->type = ptype; p->name = pname; *curr_param = p; curr_param = &p->next;
        if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA); else break;
      }
    }
    eat(l, TOKEN_RPAREN); eat(l, TOKEN_LBRACE);
    ASTNode *body = parse_statements(l); eat(l, TOKEN_RBRACE);
    FuncDefNode *node = calloc(1, sizeof(FuncDefNode));
    node->base.type = NODE_FUNC_DEF; node->name = name; node->ret_type = vtype; node->params = params_head; node->body = body;
    return (ASTNode*)node;
  } else {
    char *name_val = name;
    int is_array = 0;
    ASTNode *array_size = NULL;
    if (current_token.type == TOKEN_LBRACKET) {
      is_array = 1; eat(l, TOKEN_LBRACKET);
      if (current_token.type != TOKEN_RBRACKET) array_size = parse_expression(l);
      eat(l, TOKEN_RBRACKET);
    }
    ASTNode *init = NULL;
    if (current_token.type == TOKEN_ASSIGN) { eat(l, TOKEN_ASSIGN); init = parse_expression(l); } 
    else { if (vtype.base == TYPE_AUTO) { parser_fail(l, "'let' variable declaration must have an initializer"); } }
    eat(l, TOKEN_SEMICOLON);
    VarDeclNode *node = calloc(1, sizeof(VarDeclNode));
    node->base.type = NODE_VAR_DECL; node->var_type = vtype; node->name = name_val;
    node->initializer = init; node->is_mutable = 1; 
    node->is_array = is_array; node->array_size = array_size;
    return (ASTNode*)node;
  }
}

ASTNode* parse_program(Lexer *l) {
  safe_free_current_token();
  current_token = lexer_next_raw(l);
  
  ASTNode *head = NULL;
  ASTNode **current = &head;
  
  // Setup Recovery Buffer
  jmp_buf recover_buf;
  parser_recover_buf = &recover_buf;

  while (current_token.type != TOKEN_EOF) {
    // If an error occurs, we come back here via longjmp
    if (setjmp(recover_buf) != 0) {
        parser_sync(l);
        if (current_token.type == TOKEN_EOF) break;
    }
    
    ASTNode *node = parse_top_level(l);
    if (node) {
        if (!*current) *current = node; 
        
        // Link potential list of nodes (e.g. from import)
        ASTNode *iter = node;
        while (iter->next) iter = iter->next;
        current = &iter->next;
    }
  }
  
  parser_recover_buf = NULL; // Clear recovery
  safe_free_current_token();
  return head;
}
