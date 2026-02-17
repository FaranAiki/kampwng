#include "parser_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h> 

typedef struct MacroSig {
    char *name;
    char **params;
    int param_count;
} MacroSig;

ASTNode* parse_define(Parser *p) {
  eat(p, TOKEN_DEFINE);

  MacroSig *sigs = NULL;
  int sig_count = 0;
  int sig_cap = 0;

  do {
      if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected macro name after 'define'");
      char *macro_name = strdup(p->current_token.text);
      eat(p, TOKEN_IDENTIFIER);
      
      char **params = NULL;
      int param_count = 0;
      if (p->current_token.type == TOKEN_LPAREN) {
          eat(p, TOKEN_LPAREN);
          int cap = 4;
          params = malloc(sizeof(char*) * cap);
          
          if (p->current_token.type != TOKEN_RPAREN) {
              while(1) {
                  if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected parameter name in define definition");
                  if (param_count >= cap) { cap *= 2; params = realloc(params, sizeof(char*)*cap); }
                  params[param_count++] = strdup(p->current_token.text);
                  eat(p, TOKEN_IDENTIFIER);
                  
                  if (p->current_token.type == TOKEN_COMMA) eat(p, TOKEN_COMMA);
                  else if (p->current_token.type == TOKEN_RPAREN) break;
                  else parser_fail(p, "Expected ',' or ')' in macro parameters");
              }
          }
          eat(p, TOKEN_RPAREN);
      }

      if (sig_count >= sig_cap) {
          sig_cap = (sig_cap == 0) ? 2 : sig_cap * 2;
          sigs = realloc(sigs, sizeof(MacroSig) * sig_cap);
      }
      sigs[sig_count].name = macro_name;
      sigs[sig_count].params = params;
      sigs[sig_count].param_count = param_count;
      sig_count++;

      if (p->current_token.type == TOKEN_COMMA) {
          eat(p, TOKEN_COMMA);
      } else {
          break;
      }
  } while (1);
  
  if (p->current_token.type != TOKEN_AS) {
      const char *found = p->current_token.text;
      if (!found) {
          if (p->current_token.type == TOKEN_TYPEDEF) found = "typedef";
          else if (p->current_token.type == TOKEN_IDENTIFIER) found = "identifier";
          else found = token_type_to_string(p->current_token.type);
      }
      
      char msg[256];
      snprintf(msg, sizeof(msg), "Expected 'as' after macro definition, but found '%s'", found);
      
      // We manually construct report because parser_fail kills execution
      report_error(p->l, p->current_token, msg);
      
      char hint[256];
      snprintf(hint, sizeof(hint), "Did you mean \"define %s as %s...\"?", sigs[0].name, found);
      report_hint(p->l, p->current_token, hint);
      
      for(int i=0; i<sig_count; i++) {
          free(sigs[i].name);
          if (sigs[i].params) {
              for(int k=0; k<sigs[i].param_count; k++) free(sigs[i].params[k]);
              free(sigs[i].params);
          }
      }
      free(sigs);
      
      if (p->recover_buf) longjmp(*p->recover_buf, 1);
  }
  eat(p, TOKEN_AS);
  
  Token *body_tokens = malloc(sizeof(Token) * 32);
  int body_cap = 32;
  int body_len = 0;
  while (p->current_token.type != TOKEN_SEMICOLON && p->current_token.type != TOKEN_EOF) {
      if (body_len >= body_cap) { body_cap *= 2; body_tokens = realloc(body_tokens, sizeof(Token)*body_cap); }
      Token t = p->current_token;
      if (t.text) t.text = strdup(t.text);
      body_tokens[body_len++] = t;
      eat(p, p->current_token.type);
  }
  
  for(int i=0; i<sig_count; i++) {
      register_macro(p, sigs[i].name, sigs[i].params, sigs[i].param_count, body_tokens, body_len);
      free(sigs[i].name);
  }
  
  for(int k=0; k<body_len; k++) {
      if (body_tokens[k].text) free(body_tokens[k].text);
  }
  free(body_tokens);
  free(sigs);

  if (p->current_token.type == TOKEN_SEMICOLON) eat(p, TOKEN_SEMICOLON);

  return NULL; 
}

ASTNode* parse_typedef(Parser *p) {
  eat(p, TOKEN_TYPEDEF);
  
  int start_is_type = 0;
  TokenType ct = p->current_token.type;
  
  if ((ct >= TOKEN_KW_VOID && ct <= TOKEN_KW_LET) || 
      (ct >= TOKEN_KW_SHORT && ct <= TOKEN_KW_UNSIGNED) ||
      ct == TOKEN_STRUCT || ct == TOKEN_UNION || ct == TOKEN_ENUM || ct == TOKEN_CLASS) {
      start_is_type = 1;
  }
  else if (ct == TOKEN_IDENTIFIER) {
      if (is_typename(p, p->current_token.text)) {
          start_is_type = 1;
      }
  }

  if (start_is_type) {
      VarType target = parse_type(p);
      
      if (p->current_token.type == TOKEN_LPAREN) {
          char *cb_name = NULL;
          VarType fp_type = parse_func_ptr_decl(p, target, &cb_name);
          register_alias(p, cb_name, fp_type);
          free(cb_name);
          eat(p, TOKEN_SEMICOLON);
          return NULL;
      }
      
      if (p->current_token.type == TOKEN_AS) {
          eat(p, TOKEN_AS);
      }
      
      while(1) {
          if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected new alias name after type in typedef");
          
          char *new_name = strdup(p->current_token.text);
          eat(p, TOKEN_IDENTIFIER);
          
          VarType current_target = target;
          while (p->current_token.type == TOKEN_LBRACKET) {
              eat(p, TOKEN_LBRACKET);
              if (p->current_token.type != TOKEN_RBRACKET) {
                  ASTNode *sz = parse_expression(p);
                  if (sz && sz->type == NODE_LITERAL) current_target.array_size = ((LiteralNode*)sz)->val.int_val;
                  free_ast(sz);
              }
              eat(p, TOKEN_RBRACKET);
              current_target.ptr_depth++;
          }
          
          register_alias(p, new_name, current_target);
          free(new_name);
          
          if (p->current_token.type == TOKEN_COMMA) eat(p, TOKEN_COMMA);
          else break;
      }
  } else {
      char **names = NULL;
      int name_count = 0;
      int name_cap = 4;
      names = malloc(sizeof(char*) * name_cap);
      
      while (1) {
          if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected new type name after 'typedef'");
          if (name_count >= name_cap) { name_cap *= 2; names = realloc(names, sizeof(char*) * name_cap); }
          names[name_count++] = strdup(p->current_token.text);
          eat(p, TOKEN_IDENTIFIER);
          
          if (p->current_token.type == TOKEN_COMMA) eat(p, TOKEN_COMMA);
          else break;
      }
      
      if (p->current_token.type == TOKEN_AS) {
          eat(p, TOKEN_AS);
      } else {
          parser_fail(p, "Expected 'as' or ',' after typedef names");
      }
      
      VarType target = parse_type(p);
      if (target.base == TYPE_UNKNOWN) parser_fail(p, "Unknown type in typedef");
      
      while (p->current_token.type == TOKEN_LBRACKET) {
          eat(p, TOKEN_LBRACKET);
          if (p->current_token.type != TOKEN_RBRACKET) {
              ASTNode *sz = parse_expression(p);
              if (sz && sz->type == NODE_LITERAL) target.array_size = ((LiteralNode*)sz)->val.int_val;
              free_ast(sz);
          }
          eat(p, TOKEN_RBRACKET);
          target.ptr_depth++;
      }
      
      for (int i = 0; i < name_count; i++) {
          register_alias(p, names[i], target);
          free(names[i]);
      }
      free(names);
  }
  
  eat(p, TOKEN_SEMICOLON);
  return NULL;
}

ASTNode* parse_extern(Parser *p) {
  eat(p, TOKEN_EXTERN);
  
  if (p->current_token.type == TOKEN_CLASS || p->current_token.type == TOKEN_STRUCT || p->current_token.type == TOKEN_UNION) {
      eat(p, p->current_token.type);
      if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected name for extern class/struct/union");
      char *name = strdup(p->current_token.text);
      eat(p, TOKEN_IDENTIFIER);
      eat(p, TOKEN_SEMICOLON);
      
      register_typename(p, name, 0); 
      
      ClassNode *cn = calloc(1, sizeof(ClassNode));
      cn->base.type = NODE_CLASS;
      cn->name = name;
      cn->is_extern = 1; 
      return (ASTNode*)cn;
  }
  
  VarType ret_type = parse_type(p);
  if (ret_type.base == TYPE_UNKNOWN) { parser_fail(p, "Expected return type for extern function"); }
  if (p->current_token.type != TOKEN_IDENTIFIER) { parser_fail(p, "Expected extern function name"); }
  char *name = p->current_token.text; p->current_token.text = NULL; eat(p, TOKEN_IDENTIFIER);
  eat(p, TOKEN_LPAREN);
  Parameter *params_head = NULL; Parameter **curr_param = &params_head;
  int is_varargs = 0;
  if (p->current_token.type != TOKEN_RPAREN) {
    while (1) {
      if (p->current_token.type == TOKEN_ELLIPSIS) { eat(p, TOKEN_ELLIPSIS); is_varargs = 1; break; }
      VarType ptype = parse_type(p);
      if (ptype.base == TYPE_UNKNOWN) { parser_fail(p, "Expected parameter type"); }
      char *pname = NULL;
      if (p->current_token.type == TOKEN_IDENTIFIER) { pname = p->current_token.text; p->current_token.text = NULL; eat(p, TOKEN_IDENTIFIER); }
      
      if (p->current_token.type == TOKEN_LBRACKET) {
          eat(p, TOKEN_LBRACKET);
          if (p->current_token.type != TOKEN_RBRACKET) {
              ASTNode *sz = parse_expression(p);
              free_ast(sz);
          }
          eat(p, TOKEN_RBRACKET);
          ptype.ptr_depth++;
      }
      
      Parameter *param = calloc(1, sizeof(Parameter)); param->type = ptype; param->name = pname; *curr_param = param; curr_param = &param->next;
      if (p->current_token.type == TOKEN_COMMA) eat(p, TOKEN_COMMA); else break;
    }
  }
  eat(p, TOKEN_RPAREN); eat(p, TOKEN_SEMICOLON);
  FuncDefNode *node = calloc(1, sizeof(FuncDefNode));
  node->base.type = NODE_FUNC_DEF; node->name = name; node->ret_type = ret_type;
  node->params = params_head; node->body = NULL; node->is_varargs = is_varargs;
  return (ASTNode*)node;
}

ASTNode* parse_import(Parser *p) {
  eat(p, TOKEN_IMPORT);
  if (p->current_token.type != TOKEN_STRING) parser_fail(p, "Expected file path string after 'import'");
  char* fname = p->current_token.text;
  p->current_token.text = NULL;
  eat(p, TOKEN_STRING);
  eat(p, TOKEN_SEMICOLON);
  
  char* src = read_import_file(fname);
  if (!src) { 
      char msg[256];
      snprintf(msg, 256, "Could not open imported file: '%s'", fname);
      free(fname); 
      parser_fail(p, msg); 
  }
  
  // Create a new parser context for the imported file
  // but share the symbol tables? For now, simplistic separate parse.
  // Ideally, symbols should be shared or returned.
  // The structure `Parser` now contains symbol heads. 
  // If we want imports to affect current scope, we should share list pointers?
  // Or parse into AST and let Semantic Analysis handle symbol merging.
  
  Lexer import_l; 
  lexer_init(&import_l, p->ctx, src);
  import_l.filename = fname;
  
  Parser import_p;
  parser_init(&import_p, &import_l);
  
  ASTNode* imported_root = parse_program(&import_p);
  
  free(src);
  free(fname);
  
  return imported_root; 
}

ASTNode* parse_link(Parser *p) {
  eat(p, TOKEN_LINK);
  char *lib_name = NULL;
  if (p->current_token.type == TOKEN_IDENTIFIER || p->current_token.type == TOKEN_STRING) {
    lib_name = p->current_token.text;
    p->current_token.text = NULL;
    if (p->current_token.type == TOKEN_IDENTIFIER) eat(p, TOKEN_IDENTIFIER);
    else eat(p, TOKEN_STRING);
  } else {
    parser_fail(p, "Expected library name (string or identifier) after 'link'");
  }
  if (p->current_token.type == TOKEN_SEMICOLON) eat(p, TOKEN_SEMICOLON);
  LinkNode *node = calloc(1, sizeof(LinkNode));
  node->base.type = NODE_LINK;
  node->lib_name = lib_name;
  return (ASTNode*)node;
}

ASTNode* parse_enum(Parser *p) {
  eat(p, TOKEN_ENUM);
  if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected enum name");
  char *enum_name = strdup(p->current_token.text);
  eat(p, TOKEN_IDENTIFIER);
  register_typename(p, enum_name, 1);

  eat(p, TOKEN_LBRACKET);
  
  EnumEntry *entries_head = NULL;
  EnumEntry **curr_entry = &entries_head;
  int current_val = 0;

  while (p->current_token.type != TOKEN_RBRACKET && p->current_token.type != TOKEN_EOF) {
      if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected enum member name");
      char *member_name = strdup(p->current_token.text);
      eat(p, TOKEN_IDENTIFIER);
      
      if (p->current_token.type == TOKEN_ASSIGN) {
          eat(p, TOKEN_ASSIGN);
          int sign = 1;
          if (p->current_token.type == TOKEN_MINUS) { sign = -1; eat(p, TOKEN_MINUS); }
          if (p->current_token.type != TOKEN_NUMBER) parser_fail(p, "Expected integer value for enum member");
          current_val = p->current_token.int_val * sign;
          eat(p, TOKEN_NUMBER);
      }
      
      EnumEntry *entry = malloc(sizeof(EnumEntry));
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

  EnumNode *en = calloc(1, sizeof(EnumNode));
  en->base.type = NODE_ENUM;
  en->name = enum_name;
  en->entries = entries_head;
  return (ASTNode*)en;
}

ASTNode* parse_class(Parser *p) {  
  int is_open = 0;
  if (p->current_token.type == TOKEN_OPEN) { is_open = 1; eat(p, TOKEN_OPEN); }
  else if (p->current_token.type == TOKEN_CLOSED) { is_open = 0; eat(p, TOKEN_CLOSED); }
  
  if (p->current_token.type == TOKEN_CLASS || p->current_token.type == TOKEN_STRUCT || p->current_token.type == TOKEN_UNION) {
      int is_union = (p->current_token.type == TOKEN_UNION);
      eat(p, p->current_token.type);
      if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected name after 'class', 'struct' or 'union'");
      char *class_name = strdup(p->current_token.text);
      eat(p, TOKEN_IDENTIFIER);
      
      register_typename(p, class_name, 0); 
      
      char *parent_name = NULL;
      if (p->current_token.type == TOKEN_IS) {
          eat(p, TOKEN_IS);
          if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected parent class name after 'is'");
          parent_name = strdup(p->current_token.text);
          eat(p, TOKEN_IDENTIFIER);
      }
      
      char **traits = NULL;
      int trait_count = 0;
      if (p->current_token.type == TOKEN_HAS) {
          eat(p, TOKEN_HAS);
          int cap = 4;
          traits = malloc(sizeof(char*) * cap);
          do {
              if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected trait name after 'has'");
              if (trait_count >= cap) { cap *= 2; traits = realloc(traits, sizeof(char*)*cap); }
              traits[trait_count++] = strdup(p->current_token.text);
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
              
              FuncDefNode *func = calloc(1, sizeof(FuncDefNode));
              func->base.type = NODE_FUNC_DEF;
              func->base.line = line;
              func->base.col = col;
              func->name = strdup("iterate");
              func->ret_type = vt;
              func->params = NULL;
              func->body = body;
              func->is_open = member_open;
              func->class_name = strdup(class_name);
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
                  
                  VarDeclNode *var = calloc(1, sizeof(VarDeclNode));
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
              char *mem_name = strdup(p->current_token.text);
              eat(p, TOKEN_IDENTIFIER);
              
              if (p->current_token.type == TOKEN_LPAREN) {
                  eat(p, TOKEN_LPAREN);
                  Parameter *params = NULL;
                  Parameter **curr_p = &params;
                  
                  if (p->current_token.type != TOKEN_RPAREN) {
                      while(1) {
                          VarType pt = parse_type(p);
                          if(pt.base == TYPE_UNKNOWN) parser_fail(p, "Expected parameter type in method declaration");
                          char *pname = strdup(p->current_token.text);
                          eat(p, TOKEN_IDENTIFIER);
                          
                          if (p->current_token.type == TOKEN_LBRACKET) {
                              eat(p, TOKEN_LBRACKET);
                              if (p->current_token.type != TOKEN_RBRACKET) {
                                  ASTNode *sz = parse_expression(p);
                                  free_ast(sz);
                              }
                              eat(p, TOKEN_RBRACKET);
                              pt.ptr_depth++;
                          }
                          
                          Parameter *pm = calloc(1, sizeof(Parameter));
                          pm->type = pt; pm->name = pname;
                          *curr_p = pm; curr_p = &pm->next;
                          if (p->current_token.type == TOKEN_COMMA) eat(p, TOKEN_COMMA); else break;
                      }
                  }
                  eat(p, TOKEN_RPAREN);
                  eat(p, TOKEN_LBRACE);
                  ASTNode *body = parse_statements(p);
                  eat(p, TOKEN_RBRACE);
                  
                  FuncDefNode *func = calloc(1, sizeof(FuncDefNode));
                  func->base.type = NODE_FUNC_DEF;
                  func->base.line = line;
                  func->base.col = col;
                  func->name = mem_name;
                  func->ret_type = vt;
                  func->params = params;
                  func->body = body;
                  func->is_open = member_open;
                  func->class_name = strdup(class_name); 
                  
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
                          LiteralNode *ln = calloc(1, sizeof(LiteralNode));
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
                  
                  VarDeclNode *var = calloc(1, sizeof(VarDeclNode));
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
      
      ClassNode *cls = calloc(1, sizeof(ClassNode));
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
      char *ns_name = strdup(p->current_token.text);
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
      
      NamespaceNode *ns = calloc(1, sizeof(NamespaceNode));
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

  // Typo check omitted for brevity in this fix, can re-add

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
      
      VarDeclNode *node = calloc(1, sizeof(VarDeclNode));
      node->base.type = NODE_VAR_DECL;
      node->var_type = vtype;
      node->name = name;
      node->initializer = init;
      node->is_mutable = 1; 
      node->base.line = line; node->base.col = col;
      return (ASTNode*)node;
  }

  if (p->current_token.type != TOKEN_IDENTIFIER) { parser_fail(p, "Expected identifier definition after type"); }
  char *name = p->current_token.text; p->current_token.text = NULL; eat(p, TOKEN_IDENTIFIER);
  
  if (p->current_token.type == TOKEN_LPAREN) {
    eat(p, TOKEN_LPAREN);
    Parameter *params_head = NULL; Parameter **curr_param = &params_head;
    if (p->current_token.type != TOKEN_RPAREN) {
      while (1) {
        VarType ptype = parse_type(p);
        if (ptype.base == TYPE_UNKNOWN) parser_fail(p, "Expected parameter type in function definition");
        char *pname = p->current_token.text; p->current_token.text = NULL; eat(p, TOKEN_IDENTIFIER);
        
        if (p->current_token.type == TOKEN_LBRACKET) {
            eat(p, TOKEN_LBRACKET);
            if (p->current_token.type != TOKEN_RBRACKET) {
                ASTNode *sz = parse_expression(p);
                free_ast(sz);
            }
            eat(p, TOKEN_RBRACKET);
            ptype.ptr_depth++;
        }
        
        Parameter *pm = calloc(1, sizeof(Parameter)); pm->type = ptype; pm->name = pname; *curr_param = pm; curr_param = &pm->next;
        if (p->current_token.type == TOKEN_COMMA) eat(p, TOKEN_COMMA); else break;
      }
    }
    eat(p, TOKEN_RPAREN); eat(p, TOKEN_LBRACE);
    ASTNode *body = parse_statements(p); eat(p, TOKEN_RBRACE);
    FuncDefNode *node = calloc(1, sizeof(FuncDefNode));
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
          LiteralNode *ln = calloc(1, sizeof(LiteralNode));
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
    VarDeclNode *node = calloc(1, sizeof(VarDeclNode));
    node->base.type = NODE_VAR_DECL; node->var_type = vtype; node->name = name_val;
    node->initializer = init; node->is_mutable = 1; 
    node->is_array = is_array; node->array_size = array_size; 
    node->base.line = line; node->base.col = col;
    return (ASTNode*)node;
  }
}
