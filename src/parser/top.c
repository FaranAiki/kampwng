#include "parser_internal.h"
#include "../diagnostic/diagnostic.h" 
#include <string.h>
#include <stdlib.h>
#include <stdio.h> 

// Forward decl
void parser_advance(Lexer *l);

typedef struct MacroSig {
    char *name;
    char **params;
    int param_count;
} MacroSig;

ASTNode* parse_define(Lexer *l) {
  eat(l, TOKEN_DEFINE);

  MacroSig *sigs = NULL;
  int sig_count = 0;
  int sig_cap = 0;

  // Parse comma-separated macro signatures
  // Syntax: define Name[(params)], Name2[(params)] as Body;
  do {
      if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected macro name after 'define'");
      char *macro_name = strdup(current_token.text);
      eat(l, TOKEN_IDENTIFIER);
      
      char **params = NULL;
      int param_count = 0;
      if (current_token.type == TOKEN_LPAREN) {
          eat(l, TOKEN_LPAREN);
          int cap = 4;
          params = malloc(sizeof(char*) * cap);
          
          if (current_token.type != TOKEN_RPAREN) {
              while(1) {
                  if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected parameter name in define definition");
                  if (param_count >= cap) { cap *= 2; params = realloc(params, sizeof(char*)*cap); }
                  params[param_count++] = strdup(current_token.text);
                  eat(l, TOKEN_IDENTIFIER);
                  
                  if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA);
                  else if (current_token.type == TOKEN_RPAREN) break;
                  else parser_fail(l, "Expected ',' or ')' in macro parameters");
              }
          }
          eat(l, TOKEN_RPAREN);
      }

      if (sig_count >= sig_cap) {
          sig_cap = (sig_cap == 0) ? 2 : sig_cap * 2;
          sigs = realloc(sigs, sizeof(MacroSig) * sig_cap);
      }
      sigs[sig_count].name = macro_name;
      sigs[sig_count].params = params;
      sigs[sig_count].param_count = param_count;
      sig_count++;

      if (current_token.type == TOKEN_COMMA) {
          eat(l, TOKEN_COMMA);
      } else {
          break;
      }
  } while (1);
  
  // Explicitly check for 'as' to provide a smart error message
  if (current_token.type != TOKEN_AS) {
      const char *found = current_token.text;
      if (!found) {
          if (current_token.type == TOKEN_TYPEDEF) found = "typedef";
          else if (current_token.type == TOKEN_IDENTIFIER) found = "identifier";
          else found = token_type_to_string(current_token.type);
      }
      
      char msg[256];
      snprintf(msg, sizeof(msg), "Expected 'as' after macro definition, but found '%s'", found);
      report_error(l, current_token, msg);
      
      char hint[256];
      snprintf(hint, sizeof(hint), "Did you mean \"define %s as %s...\"?", sigs[0].name, found);
      report_hint(l, current_token, hint);
      
      // Clean up
      for(int i=0; i<sig_count; i++) {
          free(sigs[i].name);
          if (sigs[i].params) {
              for(int p=0; p<sigs[i].param_count; p++) free(sigs[i].params[p]);
              free(sigs[i].params);
          }
      }
      free(sigs);
      
      if (parser_recover_buf) longjmp(*parser_recover_buf, 1);
      // exit(1);
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
  
  // Register all collected macros with the same body FIRST
  for(int i=0; i<sig_count; i++) {
      register_macro(sigs[i].name, sigs[i].params, sigs[i].param_count, body_tokens, body_len);
      free(sigs[i].name);
      // Note: params ownership is transferred to register_macro
  }
  
  // Free local body tokens (register_macro deep copies them)
  for(int k=0; k<body_len; k++) {
      if (body_tokens[k].text) free(body_tokens[k].text);
  }
  free(body_tokens);
  free(sigs);

  // Consume the semicolon AFTER registering the macro.
  // This ensures that when the Lexer fetches the next token, 
  // it is aware of the newly registered macro and can expand it.
  if (current_token.type == TOKEN_SEMICOLON) eat(l, TOKEN_SEMICOLON);

  return NULL; 
}

ASTNode* parse_typedef(Lexer *l) {
  eat(l, TOKEN_TYPEDEF);
  
  // Allow C-style: typedef Type Name;
  // We check if the current token looks like a type to discern usage.
  // Note: This is heuristic.
  if (current_token.type >= TOKEN_KW_INT && current_token.type <= TOKEN_KW_VOID) {
      // C-style likely
      VarType target = parse_type(l);
      
      while(1) {
          if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected new type name after 'typedef'");
          char *new_name = strdup(current_token.text);
          eat(l, TOKEN_IDENTIFIER);
          
          VarType current_target = target;
          // Handle C-style array: typedef int x[10];
          while (current_token.type == TOKEN_LBRACKET) {
              eat(l, TOKEN_LBRACKET);
              if (current_token.type != TOKEN_RBRACKET) {
                  ASTNode *sz = parse_expression(l);
                  if (sz && sz->type == NODE_LITERAL) current_target.array_size = ((LiteralNode*)sz)->val.int_val;
                  free_ast(sz);
              }
              eat(l, TOKEN_RBRACKET);
              current_target.ptr_depth++;
          }
          
          register_alias(new_name, current_target);
          free(new_name);
          
          if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA);
          else break;
      }
      
      eat(l, TOKEN_SEMICOLON);
      return NULL;
  }
  
  // Default Style: typedef Name1, Name2 as Type;
  char **names = NULL;
  int name_count = 0;
  int name_cap = 4;
  names = malloc(sizeof(char*) * name_cap);
  
  while (1) {
      if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected new type name after 'typedef'");
      if (name_count >= name_cap) { name_cap *= 2; names = realloc(names, sizeof(char*) * name_cap); }
      names[name_count++] = strdup(current_token.text);
      eat(l, TOKEN_IDENTIFIER);
      
      if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA);
      else break;
  }
  
  if (current_token.type == TOKEN_AS) {
      eat(l, TOKEN_AS);
  } else {
      parser_fail(l, "Expected 'as' or ',' after typedef names");
  }
  
  VarType target = parse_type(l);
  if (target.base == TYPE_UNKNOWN) parser_fail(l, "Unknown type in typedef");
  
  while (current_token.type == TOKEN_LBRACKET) {
      eat(l, TOKEN_LBRACKET);
      if (current_token.type != TOKEN_RBRACKET) {
          ASTNode *sz = parse_expression(l);
          if (sz && sz->type == NODE_LITERAL) target.array_size = ((LiteralNode*)sz)->val.int_val;
          free_ast(sz);
      }
      eat(l, TOKEN_RBRACKET);
      target.ptr_depth++;
  }
  
  for (int i = 0; i < name_count; i++) {
      register_alias(names[i], target);
      free(names[i]);
  }
  free(names);
  
  eat(l, TOKEN_SEMICOLON);
  return NULL;
}

ASTNode* parse_extern(Lexer *l) {
  eat(l, TOKEN_EXTERN);
  
  // Check for 'extern class/struct FILE;' opaque type declarations
  if (current_token.type == TOKEN_CLASS || current_token.type == TOKEN_STRUCT || current_token.type == TOKEN_UNION) {
      eat(l, current_token.type);
      if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected name for extern class/struct/union");
      char *name = strdup(current_token.text);
      eat(l, TOKEN_IDENTIFIER);
      eat(l, TOKEN_SEMICOLON);
      
      register_typename(name, 0); // Register as a usable type
      
      ClassNode *cn = calloc(1, sizeof(ClassNode));
      cn->base.type = NODE_CLASS;
      cn->name = name;
      cn->is_extern = 1; // Mark as opaque
      return (ASTNode*)cn;
  }
  
  // Regular extern function declaration
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

ASTNode* parse_import(Lexer *l) {
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
  
  Token saved_token = current_token;
  current_token.text = NULL; current_token.type = TOKEN_UNKNOWN; 
  Lexer import_l; lexer_init(&import_l, src);
  import_l.filename = fname; // Set filename on the imported lexer
  
  ASTNode* imported_root = parse_program(&import_l);
  
  free(src);
  free(fname); // Can free filename string here since import_l is stack allocated and done
  
  current_token = saved_token;
  return imported_root; 

}

ASTNode* parse_link(Lexer *l) {
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

ASTNode* parse_enum(Lexer *l) {
  eat(l, TOKEN_ENUM);
  if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected enum name");
  char *enum_name = strdup(current_token.text);
  eat(l, TOKEN_IDENTIFIER);
  register_typename(enum_name, 1); // Register as ENUM (is_enum = 1)

  // Maybe use [ ] instead of { } ? 
  // Because { } is for code, whereas [ ] is for members
  eat(l, TOKEN_LBRACKET);
  
  EnumEntry *entries_head = NULL;
  EnumEntry **curr_entry = &entries_head;
  int current_val = 0;

  while (current_token.type != TOKEN_RBRACKET && current_token.type != TOKEN_EOF) {
      if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected enum member name");
      char *member_name = strdup(current_token.text);
      eat(l, TOKEN_IDENTIFIER);
      
      if (current_token.type == TOKEN_ASSIGN) {
          eat(l, TOKEN_ASSIGN);
          int sign = 1;
          if (current_token.type == TOKEN_MINUS) { sign = -1; eat(l, TOKEN_MINUS); }
          if (current_token.type != TOKEN_NUMBER) parser_fail(l, "Expected integer value for enum member");
          current_val = current_token.int_val * sign;
          eat(l, TOKEN_NUMBER);
      }
      
      EnumEntry *entry = malloc(sizeof(EnumEntry));
      entry->name = member_name;
      entry->value = current_val;
      entry->next = NULL;
      *curr_entry = entry;
      curr_entry = &entry->next;
      
      current_val++;
      
      if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA);
      else if (current_token.type != TOKEN_RBRACKET) parser_fail(l, "Expected ',' or ']' in enum definition");
  }
  eat(l, TOKEN_RBRACKET);
  // DO anything aftern enum 
  /*
  * enum Car[Toyota, Honda, ...] ... ;
  */
  eat(l, TOKEN_SEMICOLON);

  EnumNode *en = calloc(1, sizeof(EnumNode));
  en->base.type = NODE_ENUM;
  en->name = enum_name;
  en->entries = entries_head;
  return (ASTNode*)en;
}

ASTNode* parse_class(Lexer *l) {  
  int is_open = 0;
  if (current_token.type == TOKEN_OPEN) { is_open = 1; eat(l, TOKEN_OPEN); }
  else if (current_token.type == TOKEN_CLOSED) { is_open = 0; eat(l, TOKEN_CLOSED); }
  
  if (current_token.type == TOKEN_CLASS || current_token.type == TOKEN_STRUCT || current_token.type == TOKEN_UNION) {
      int is_union = (current_token.type == TOKEN_UNION);
      eat(l, current_token.type);
      if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected name after 'class', 'struct' or 'union'");
      char *class_name = strdup(current_token.text);
      eat(l, TOKEN_IDENTIFIER);
      
      register_typename(class_name, 0); // Register as CLASS (is_enum = 0)
      
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
          int line = current_token.line;
          int col = current_token.col;
          if (current_token.type == TOKEN_OPEN) { member_open = 1; eat(l, TOKEN_OPEN); }
          else if (current_token.type == TOKEN_CLOSED) { member_open = 0; eat(l, TOKEN_CLOSED); }
          
          if (current_token.type == TOKEN_FLUX) {
              // Anonymous Flux in Class
              // flux Type { ... }
              eat(l, TOKEN_FLUX);
              VarType vt = parse_type(l);
              
              eat(l, TOKEN_LBRACE);
              ASTNode *body = parse_statements(l);
              eat(l, TOKEN_RBRACE);
              
              FuncDefNode *func = calloc(1, sizeof(FuncDefNode));
              func->base.type = NODE_FUNC_DEF;
              func->base.line = line;
              func->base.col = col;
              func->name = strdup("iterate"); // Special name for anonymous flux
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
                  func->base.line = line;
                  func->base.col = col;
                  func->name = mem_name;
                  func->ret_type = vt;
                  func->params = params;
                  func->body = body;
                  func->is_open = member_open;
                  func->class_name = strdup(class_name); // Link Method to correct class context for semcheck
                  
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
      cls->is_union = is_union;
      
      return (ASTNode*)cls;
  }
  return NULL;
}

ASTNode* parse_top_level(Lexer *l) { 
  // Empty semicolon
  if (current_token.type == TOKEN_SEMICOLON) {
      eat(l, TOKEN_SEMICOLON);
      return NULL;
  }

  if (current_token.type == TOKEN_NAMESPACE) {
      eat(l, TOKEN_NAMESPACE);
      // TODO: this is when the namespace {} (without identifier)
      if (current_token.type != TOKEN_IDENTIFIER) parser_fail(l, "Expected namespace name");
      char *ns_name = strdup(current_token.text);
      eat(l, TOKEN_IDENTIFIER);
      // TODO maybe namespace .. as ... { } ?

      eat(l, TOKEN_LBRACE);
      
      ASTNode *body_head = NULL;
      ASTNode **body_curr = &body_head;
      
      while(current_token.type != TOKEN_RBRACE && current_token.type != TOKEN_EOF) {
          ASTNode *n = parse_top_level(l);
          if (n) {
              *body_curr = n;
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
    return parse_define(l);
  }

  if (current_token.type == TOKEN_TYPEDEF) {
    return parse_typedef(l);
  }
  
  if (current_token.type == TOKEN_ENUM) {
    return parse_enum(l);
  }
  if (current_token.type == TOKEN_CLASS || 
      current_token.type == TOKEN_STRUCT || 
      current_token.type == TOKEN_UNION || 
      (current_token.type == TOKEN_OPEN) || 
      (current_token.type == TOKEN_CLOSED)) {
    return parse_class(l);
  }

  if (current_token.type == TOKEN_LINK) {
    return parse_link(l);
  }

  if (current_token.type == TOKEN_IMPORT) {
    return parse_import(l);
  }
  
  // 3. EXTERN (FFI)
  if (current_token.type == TOKEN_EXTERN) {
    return parse_extern(l);
  }

  if (current_token.type == TOKEN_KW_MUT || current_token.type == TOKEN_KW_IMUT) {
    return parse_var_decl_internal(l);
  }

  // Capture location before parsing type
  int line = current_token.line;
  int col = current_token.col;

  // SMART TYPO CHECK (Optimized to be less aggressive)
  if (current_token.type == TOKEN_IDENTIFIER) {
      const char *top_kws[] = {
          "typedef", "namespace", "define", "class", "import", "link", "extern", 
          "struct", "enum", "const", "let", "mut", "imut", "return", "if", "while", "flux", "union", NULL
      };
      
      const char *best_kw = NULL;
      int min_dist = 2; // Strict minimal distance
      
      size_t len = strlen(current_token.text);
      if (len > 3) { // Only check if token has enough length
          for(int i=0; top_kws[i]; i++) {
              int d = levenshtein_dist(current_token.text, top_kws[i]);
              if (d < min_dist) {
                  // Additional Check: Don't trigger if length difference is too big
                  size_t kw_len = strlen(top_kws[i]);
                  if (abs((int)len - (int)kw_len) <= 1) { 
                      min_dist = d;
                      best_kw = top_kws[i];
                  }
              }
          }
      }

      if (best_kw) {
           char err_msg[256];
           snprintf(err_msg, 256, "Invalid token '%s'", current_token.text);
           report_error(l, current_token, err_msg);
           
           char hint_msg[256];
           snprintf(hint_msg, 256, "'%s' looks like keyword '%s'. Did you mean to use %s?", current_token.text, best_kw, best_kw);
           report_hint(l, current_token, hint_msg);
           
           if (parser_recover_buf) longjmp(*parser_recover_buf, 1);
           exit(1); 
      }
  }

  // Parse Function or Flux
  int is_flux = 0;
  if (current_token.type == TOKEN_FLUX) {
      is_flux = 1;
      eat(l, TOKEN_FLUX);
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
    node->is_flux = is_flux; // Mark as flux if applicable
    // Set location
    node->base.line = line; node->base.col = col;
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
    // Set location
    node->base.line = line; node->base.col = col;
    return (ASTNode*)node;
  }
}
