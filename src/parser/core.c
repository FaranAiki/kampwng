#include "parser_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

Token current_token = {TOKEN_UNKNOWN, NULL, 0, 0, 0.0};
jmp_buf *parser_env = NULL;         // REPL
jmp_buf *parser_recover_buf = NULL; // Compilation

// Why is the type of this shit looks like this lmaoo

typedef struct Macro {
    char *name;
    char **params;
    int param_count;
    Token *body;
    int body_len;
    struct Macro *next;
} Macro;

Macro *macro_head = NULL;

typedef struct TypeName {
    char *name;
    int is_enum; 
    struct TypeName *next;
} TypeName;

TypeName *type_head = NULL;

void register_typename(const char *name, int is_enum) {
    TypeName *t = malloc(sizeof(TypeName));
    t->name = strdup(name);
    t->is_enum = is_enum;
    t->next = type_head;
    type_head = t;
}

int is_typename(const char *name) {
    TypeName *cur = type_head;
    while(cur) {
        if (strcmp(cur->name, name) == 0) return 1;
        cur = cur->next;
    }
    return 0;
}

int get_typename_kind(const char *name) {
    TypeName *cur = type_head;
    while(cur) {
        if (strcmp(cur->name, name) == 0) return cur->is_enum ? 2 : 1;
        cur = cur->next;
    }
    return 0;
}

typedef struct TypeAlias {
    char *name;
    VarType target;
    struct TypeAlias *next;
} TypeAlias;

TypeAlias *alias_head = NULL;

void register_alias(const char *name, VarType target) {
    // Overwrite if exists, or push new
    // TODO give info/warning
    TypeAlias *curr = alias_head;
    while(curr) {
        if (strcmp(curr->name, name) == 0) {
            curr->target = target;
            return;
        }
        curr = curr->next;
    }

    TypeAlias *a = malloc(sizeof(TypeAlias));
    a->name = strdup(name);
    a->target = target;
    if (target.class_name) a->target.class_name = strdup(target.class_name);
    
    a->next = alias_head;
    alias_head = a;
}

VarType* get_alias(const char *name) {
    TypeAlias *curr = alias_head;
    while(curr) {
        if (strcmp(curr->name, name) == 0) return &curr->target;
        curr = curr->next;
    }
    return NULL;
}

typedef struct Expansion {
    Token *tokens;
    int count;
    int pos;
    struct Expansion *next;
} Expansion;

Expansion *expansion_head = NULL;

Token token_clone(Token t) {
    Token new_t = t;
    if (t.text) new_t.text = strdup(t.text);
    return new_t;
}

void register_macro(const char *name, char **params, int param_count, Token *body, int body_len) {
    Macro *m = malloc(sizeof(Macro));
    m->name = strdup(name);
    m->params = params; 
    m->param_count = param_count;
    m->body = malloc(sizeof(Token) * body_len);
    for (int i=0; i<body_len; i++) {
        m->body[i] = token_clone(body[i]);
    }
    m->body_len = body_len;
    m->next = macro_head;
    macro_head = m;
}

Macro* find_macro(const char *name) {
    Macro *curr = macro_head;
    while(curr) {
        if (strcmp(curr->name, name) == 0) return curr;
        curr = curr->next;
    }
    return NULL;
}

void free_token_seq(Token *tokens, int count) {
    for(int i=0; i<count; i++) {
        if (tokens[i].text) free(tokens[i].text);
    }
    free(tokens);
}

// --- TOKEN STREAM MANAGEMENT ---

Token lexer_next_raw(Lexer *l) {
    return lexer_next(l);
}

Token get_next_token_expanded(Lexer *l) {
    if (expansion_head) {
        if (expansion_head->pos < expansion_head->count) {
            return token_clone(expansion_head->tokens[expansion_head->pos++]);
        } else {
            Expansion *finished = expansion_head;
            expansion_head = expansion_head->next;
            free_token_seq(finished->tokens, finished->count); 
            free(finished);
            return get_next_token_expanded(l);
        }
    }
    return lexer_next(l);
}

Token fetch_safe(Lexer *l) { return get_next_token_expanded(l); }

void parser_set_recovery(jmp_buf *env) { parser_env = env; }

void safe_free_current_token() {
  if (current_token.text) { free(current_token.text); current_token.text = NULL; }
}

void parser_fail_at(Lexer *l, Token t, const char *msg) {
    report_error(l, t, msg);
    l->ctx->parser_error_count++;
    safe_free_current_token();
    
    if (parser_recover_buf) {
        longjmp(*parser_recover_buf, 1);
    } else if (parser_env) {
        longjmp(*parser_env, 1);
    } else {
        // TODO make this not die
        exit(1);
    }
}

void parser_fail(Lexer *l, const char *msg) {
    parser_fail_at(l, current_token, msg);
}

void parser_reset(void) {
    safe_free_current_token();
    current_token.type = TOKEN_UNKNOWN;
    current_token.int_val = 0; 
    current_token.long_val = 0;
    current_token.double_val = 0.0;
    current_token.line = 0; current_token.col = 0;
    parser_env = NULL;
    parser_recover_buf = NULL;
    // parser_error_count = 0;
}

// use algo to not automatically crash 
void parser_sync(Lexer *l) {
    while (current_token.type != TOKEN_EOF) {
        if (current_token.type == TOKEN_SEMICOLON) {
            eat(l, TOKEN_SEMICOLON);
            return;
        }
        if (current_token.type == TOKEN_RBRACE) {
            eat(l, TOKEN_RBRACE);
            return;
        }
        switch (current_token.type) {
            // skip all case
            case TOKEN_CLASS:
            case TOKEN_STRUCT:
            case TOKEN_UNION: // Skip union
            case TOKEN_NAMESPACE:
            case TOKEN_KW_INT:
            case TOKEN_KW_VOID:
            case TOKEN_KW_CHAR:
            case TOKEN_KW_BOOL:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_LOOP:
            case TOKEN_RETURN:
            case TOKEN_KW_LET:
            case TOKEN_DEFINE:
                return;
            default:
                eat(l, current_token.type); 
        }
    }
}

void eat(Lexer *l, TokenType type) {
  if (current_token.type == type) {
    safe_free_current_token();
    Token t = fetch_safe(l);
    
    while (t.type == TOKEN_IDENTIFIER) {
        Macro *m = find_macro(t.text);
        if (!m) break; 
        
        Token **args = NULL;
        int *arg_lens = NULL;
        
        if (m->param_count > 0) {
            Token peek = fetch_safe(l);
            if (peek.type != TOKEN_LPAREN) {
                parser_fail(l, "Function-like macro requires arguments list '('.");
            }
            if(peek.text) free(peek.text);

            args = malloc(sizeof(Token*) * m->param_count);
            arg_lens = calloc(m->param_count, sizeof(int));
            
            for(int i=0; i<m->param_count; i++) {
                int cap = 8; int len = 0;
                args[i] = malloc(sizeof(Token) * cap);
                int depth = 0;
                while(1) {
                    Token arg_t = fetch_safe(l);
                    if (arg_t.type == TOKEN_EOF) parser_fail(l, "Unexpected EOF in macro arguments");
                    
                    if (arg_t.type == TOKEN_LPAREN) depth++;
                    else if (arg_t.type == TOKEN_RPAREN) {
                        if (depth == 0) {
                            if (i == m->param_count - 1) {
                                if(arg_t.text) free(arg_t.text); 
                                break; 
                            }
                            depth--; 
                        } else depth--;
                    }
                    else if (arg_t.type == TOKEN_COMMA) {
                        if (depth == 0) {
                            if (i < m->param_count - 1) {
                                if(arg_t.text) free(arg_t.text);
                                break;
                            }
                        }
                    }
                    
                    if (len >= cap) { cap *= 2; args[i] = realloc(args[i], sizeof(Token)*cap); }
                    args[i][len++] = arg_t;
                }
                arg_lens[i] = len;
            }
        }
        
        int res_cap = m->body_len * 2 + 16;
        int res_len = 0;
        Token *res = malloc(sizeof(Token) * res_cap);
        
        for(int i=0; i<m->body_len; i++) {
            Token bt = m->body[i];
            int p_idx = -1;
            if (bt.type == TOKEN_IDENTIFIER && m->param_count > 0) {
                for(int k=0; k<m->param_count; k++) {
                    if (strcmp(bt.text, m->params[k]) == 0) { p_idx = k; break; }
                }
            }
            
            if (p_idx != -1) {
                for(int k=0; k<arg_lens[p_idx]; k++) {
                    if (res_len >= res_cap) { res_cap *= 2; res = realloc(res, sizeof(Token)*res_cap); }
                    res[res_len++] = token_clone(args[p_idx][k]);
                }
            } else {
                if (res_len >= res_cap) { res_cap *= 2; res = realloc(res, sizeof(Token)*res_cap); }
                res[res_len++] = token_clone(bt);
            }
        }
        
        if (args) {
            for(int i=0; i<m->param_count; i++) {
                for(int k=0; k<arg_lens[i]; k++) {
                    if (args[i][k].text) free(args[i][k].text);
                }
                free(args[i]);
            }
            free(args);
            free(arg_lens);
        }
        
        if (t.text) free(t.text); 
        
        Expansion *ex = malloc(sizeof(Expansion));
        ex->tokens = res;
        ex->count = res_len;
        ex->pos = 0;
        ex->next = expansion_head;
        expansion_head = ex;
        
        t = fetch_safe(l);
    }
    
    current_token = t;

  } else {
    char msg[256];
    const char *expected = get_token_description(type);
    const char *found = current_token.type == TOKEN_EOF ? "end of file" : 
                        (current_token.text ? current_token.text : token_type_to_string(current_token.type));
    
    // TODO add more token casing
    // TODO use switch statement
    if (type == TOKEN_SEMICOLON) {
        snprintf(msg, sizeof(msg), "Expected ';' after statement, but found '%s'", found);
    } else if (type == TOKEN_RPAREN) {
        snprintf(msg, sizeof(msg), "Expected ')' to close expression, but found '%s'", found);
    } else if (type == TOKEN_RBRACE) {
        snprintf(msg, sizeof(msg), "Expected '}' to close block, but found '%s'", found);
    } else if (type == TOKEN_RBRACKET) {
        snprintf(msg, sizeof(msg), "Expected ']' to close array/enum, but found '%s'", found);
    } else if (type == TOKEN_LPAREN) {
        snprintf(msg, sizeof(msg), "Expected '(' to start expression, but found '%s'", found);
    } else if (type == TOKEN_LBRACE) {
        snprintf(msg, sizeof(msg), "Expected '{' to start block, but found '%s'", found);
    } else if (type == TOKEN_LBRACKET) {
        snprintf(msg, sizeof(msg), "Expected '[' to start array/enum, but found '%s'", found);
    } else {
        snprintf(msg, sizeof(msg), "Expected '%s' but found '%s'", expected, found);
    } 
    parser_fail(l, msg);
  }
}

// Composite type parsing helper: handling sequences like `unsigned long long`
VarType parse_type(Lexer *l) {
  VarType t = {TYPE_UNKNOWN, 0, NULL, 0, 0, 0, NULL, NULL, 0, 0}; 

  if (current_token.type == TOKEN_KW_UNSIGNED) {
      t.is_unsigned = 1;
      eat(l, TOKEN_KW_UNSIGNED);
  }

  if (current_token.type == TOKEN_IDENTIFIER) {
      // Check Alias First
      VarType *alias = get_alias(current_token.text);
      if (alias) {
          t = *alias; // Copy alias definition
          if (t.class_name) t.class_name = strdup(t.class_name);
          eat(l, TOKEN_IDENTIFIER);
      }
      else {
          int kind = get_typename_kind(current_token.text);
          if (kind != 0) {
              if (kind == 2) { 
                  // Enum - treat as TYPE_ENUM so Semantic Analysis knows it's an Enum
                  t.base = TYPE_ENUM;
                  t.class_name = strdup(current_token.text);
              } else {
                  // Class
                  t.base = TYPE_CLASS;
                  t.class_name = strdup(current_token.text);
              }
              eat(l, TOKEN_IDENTIFIER);
          } else {
              return t; // Unknown
          }
      }
  } else {
      if (current_token.type == TOKEN_KW_INT) { t.base = TYPE_INT; eat(l, TOKEN_KW_INT); }
      else if (current_token.type == TOKEN_KW_SHORT) { t.base = TYPE_SHORT; eat(l, TOKEN_KW_SHORT); }
      else if (current_token.type == TOKEN_KW_LONG) {
          eat(l, TOKEN_KW_LONG);
          if (current_token.type == TOKEN_KW_LONG) {
              eat(l, TOKEN_KW_LONG);
              if (current_token.type == TOKEN_KW_DOUBLE) {
                  eat(l, TOKEN_KW_DOUBLE);
                  t.base = TYPE_LONG_DOUBLE;
              } else {
                  t.base = TYPE_LONG_LONG;
              }
          } else if (current_token.type == TOKEN_KW_DOUBLE) {
              eat(l, TOKEN_KW_DOUBLE);
              t.base = TYPE_LONG_DOUBLE;
          } else if (current_token.type == TOKEN_KW_INT) {
              eat(l, TOKEN_KW_INT);
              t.base = TYPE_LONG;
          } else {
              t.base = TYPE_LONG;
          }
      }
      else if (current_token.type == TOKEN_KW_DOUBLE) {
          eat(l, TOKEN_KW_DOUBLE);
          if (current_token.type == TOKEN_KW_LONG) {
              eat(l, TOKEN_KW_LONG);
              if (current_token.type == TOKEN_KW_LONG) {
                   eat(l, TOKEN_KW_LONG); // double long long
              }
              t.base = TYPE_LONG_DOUBLE;
          } else {
              t.base = TYPE_DOUBLE;
          }
      }
      else if (current_token.type == TOKEN_KW_CHAR) { t.base = TYPE_CHAR; eat(l, TOKEN_KW_CHAR); }
      else if (current_token.type == TOKEN_KW_BOOL) { t.base = TYPE_BOOL; eat(l, TOKEN_KW_BOOL); }
      else if (current_token.type == TOKEN_KW_SINGLE) { t.base = TYPE_FLOAT; eat(l, TOKEN_KW_SINGLE); }
      else if (current_token.type == TOKEN_KW_STRING) { t.base = TYPE_STRING; eat(l, TOKEN_KW_STRING); }
      else if (current_token.type == TOKEN_KW_VOID) { t.base = TYPE_VOID; eat(l, TOKEN_KW_VOID); }
      else if (current_token.type == TOKEN_KW_LET) { t.base = TYPE_AUTO; eat(l, TOKEN_KW_LET); }
      else {
          if (t.is_unsigned) {
              // If just `unsigned`, defaults to `unsigned int`
              t.base = TYPE_INT; 
          } else {
              return t; // Unknown
          }
      }
  }

  while (current_token.type == TOKEN_STAR) {
    t.ptr_depth++;
    eat(l, TOKEN_STAR);
  }
  
  return t;
}

// Parses: (*Name)(Type, Type, ...)
VarType parse_func_ptr_decl(Lexer *l, VarType ret_type, char **out_name) {
    VarType vt = {0};
    vt.is_func_ptr = 1;
    vt.fp_ret_type = malloc(sizeof(VarType));
    *vt.fp_ret_type = ret_type;
    
    eat(l, TOKEN_LPAREN);
    eat(l, TOKEN_STAR);
    
    if (current_token.type != TOKEN_IDENTIFIER) {
        parser_fail(l, "Expected identifier in function pointer declaration");
    }
    
    if (out_name) *out_name = strdup(current_token.text);
    eat(l, TOKEN_IDENTIFIER);
    
    eat(l, TOKEN_RPAREN);
    eat(l, TOKEN_LPAREN);
    
    int cap = 4;
    vt.fp_param_types = malloc(sizeof(VarType) * cap);
    vt.fp_param_count = 0;
    
    if (current_token.type != TOKEN_RPAREN) {
        while(1) {
            if (current_token.type == TOKEN_ELLIPSIS) {
                vt.fp_is_varargs = 1;
                eat(l, TOKEN_ELLIPSIS);
                break;
            }
            
            VarType pt = parse_type(l);
            if (pt.base == TYPE_UNKNOWN) parser_fail(l, "Expected type in function pointer params");
            
            // Optional parameter name (ignored in type signature but allowed syntax)
            if (current_token.type == TOKEN_IDENTIFIER) {
                eat(l, TOKEN_IDENTIFIER); 
            }
            
            // Handle array params
             if (current_token.type == TOKEN_LBRACKET) {
                eat(l, TOKEN_LBRACKET);
                if (current_token.type != TOKEN_RBRACKET) {
                    // Ignore size expression for simple func ptr parsing
                    // In a full implementation we'd parse expression
                     ASTNode* tmp = parse_expression(l);
                     free_ast(tmp);
                }
                eat(l, TOKEN_RBRACKET);
                pt.ptr_depth++;
            }
            
            if (vt.fp_param_count >= cap) {
                cap *= 2;
                vt.fp_param_types = realloc(vt.fp_param_types, sizeof(VarType) * cap);
            }
            vt.fp_param_types[vt.fp_param_count++] = pt;
            
            if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA);
            else break;
        }
    }
    eat(l, TOKEN_RPAREN);
    
    return vt;
}

// Helper to read file content
char* read_file_content(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(len + 1);
    if(buf) { fread(buf, 1, len, f); buf[len] = 0; }
    fclose(f);
    return buf;
}

// TODO add more extensions (?)
char* read_import_file(const char* filename) {
  // Smart Import Logic
  
  const char* paths[] = { "", "lib/" };
  // prioritize .aky because .aky statistically imports from .hky
  const char* exts[] = { ".aky", ".hky", "" };
  
  char path[1024];
  
  for (int i = 0; i < 2; i++) {
      for (int j = 0; j < 3; j++) {
          snprintf(path, sizeof(path), "%s%s%s", paths[i], filename, exts[j]);
          char *content = read_file_content(path);
          if (content) return content;
      }
  }
  
  return NULL;
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
   
    debug_flow("Current token type: %s", token_type_to_string(current_token.type));

    ASTNode *node = parse_top_level(l);
    if (node) {
        if (!*current) *current = node; 
        
        // Link potential list of nodes (e.g. from import)
        ASTNode *iter = node;
        while (iter->next) iter = iter->next;
        current = &iter->next;
    }

    debug_flow("Token consumed safely");
  }
  
  parser_recover_buf = NULL; // Clear recovery
  safe_free_current_token();
  return head;
}

const char* get_node_type_string(NodeType type) {
    switch (type) {
        case NODE_ROOT:          return "Root";
        case NODE_FUNC_DEF:      return "FunctionDefinition";
        case NODE_CALL:          return "Call";
        case NODE_RETURN:        return "Return";
        case NODE_BREAK:         return "Break";
        case NODE_CONTINUE:      return "Continue";
        
        // Control Flow
        case NODE_LOOP:          return "Loop";
        case NODE_WHILE:         return "While";
        case NODE_IF:            return "If";
        case NODE_SWITCH:        return "Switch";
        case NODE_CASE:          return "Case";
        
        // Variables & Assignment
        case NODE_VAR_DECL:      return "VarDecl";
        case NODE_ASSIGN:        return "Assignment";
        case NODE_VAR_REF:       return "VarRef";
        
        // Operations
        case NODE_BINARY_OP:     return "BinaryOp";
        case NODE_UNARY_OP:      return "UnaryOp";
        case NODE_INC_DEC:       return "IncDec";
        case NODE_CAST:          return "Cast";
        
        // Data Structures
        case NODE_LITERAL:       return "Literal";
        case NODE_ARRAY_LIT:     return "ArrayLiteral";
        case NODE_ARRAY_ACCESS:  return "ArrayAccess";
        
        // OOP & Modular
        case NODE_LINK:          return "Link";
        case NODE_CLASS:         return "Class";
        case NODE_NAMESPACE:     return "Namespace";
        case NODE_ENUM:          return "Enum";
        case NODE_MEMBER_ACCESS: return "MemberAccess";
        case NODE_METHOD_CALL:   return "MethodCall";
        case NODE_TRAIT_ACCESS:  return "TraitAccess";
        
        // Reflection / Metaprogramming
        case NODE_TYPEOF:        return "Typeof";
        case NODE_HAS_METHOD:    return "HasMethod";
        case NODE_HAS_ATTRIBUTE: return "HasAttribute";
        
        // Flux / Generator
        case NODE_EMIT:          return "Emit";
        case NODE_FOR_IN:        return "ForIn";
        
        default:                 return "UnknownNode";
    }
}

void free_ast(ASTNode *node) {
  if (!node) return;
  if (node->next) free_ast(node->next);
  switch (node->type) {
    case NODE_TYPEOF: { UnaryOpNode *u = (UnaryOpNode*)node; free_ast(u->operand); break; }
    case NODE_MEMBER_ACCESS: { MemberAccessNode *m = (MemberAccessNode*)node; free_ast(m->object); if (m->member_name) free(m->member_name); break; }
    case NODE_CLASS: { ClassNode *c = (ClassNode*)node; free(c->name); if (c->parent_name) free(c->parent_name); if (c->traits.names) { for(int i=0; i<c->traits.count; i++) free(c->traits.names[i]); free(c->traits.names); } free_ast(c->members); break; }
    case NODE_NAMESPACE: { NamespaceNode *n = (NamespaceNode*)node; free(n->name); free_ast(n->body); break; }
    case NODE_FUNC_DEF: { FuncDefNode *f = (FuncDefNode*)node; if (f->name) free(f->name); Parameter *p = f->params; while (p) { Parameter *next = p->next; if (p->name) free(p->name); free(p); p = next; } free_ast(f->body); break; }
    case NODE_VAR_DECL: { VarDeclNode *v = (VarDeclNode*)node; if (v->name) free(v->name); if (v->var_type.class_name) free(v->var_type.class_name); free_ast(v->initializer); free_ast(v->array_size); break; }
    case NODE_ASSIGN: { AssignNode *a = (AssignNode*)node; if (a->name) free(a->name); free_ast(a->value); free_ast(a->index); free_ast(a->target); break; }
    case NODE_VAR_REF: { VarRefNode *v = (VarRefNode*)node; if (v->name) free(v->name); break; }
    case NODE_ARRAY_ACCESS: { ArrayAccessNode *a = (ArrayAccessNode*)node; free_ast(a->target); free_ast(a->index); break; }
    case NODE_CALL: { CallNode *c = (CallNode*)node; if (c->name) free(c->name); free_ast(c->args); break; }
    case NODE_RETURN: { ReturnNode *r = (ReturnNode*)node; free_ast(r->value); break; }
    case NODE_IF: { IfNode *i = (IfNode*)node; free_ast(i->condition); free_ast(i->then_body); free_ast(i->else_body); break; }
    case NODE_WHILE: { WhileNode *w = (WhileNode*)node; free_ast(w->condition); free_ast(w->body); break; }
    case NODE_LOOP: { LoopNode *l = (LoopNode*)node; free_ast(l->iterations); free_ast(l->body); break; }
    case NODE_BINARY_OP: { BinaryOpNode *b = (BinaryOpNode*)node; free_ast(b->left); free_ast(b->right); break; }
    case NODE_UNARY_OP: { UnaryOpNode *u = (UnaryOpNode*)node; free_ast(u->operand); break; }
    case NODE_ARRAY_LIT: { ArrayLitNode *a = (ArrayLitNode*)node; free_ast(a->elements); break; }
    case NODE_LINK: { LinkNode *l = (LinkNode*)node; if (l->lib_name) free(l->lib_name); break; }
    case NODE_LITERAL: { LiteralNode *l = (LiteralNode*)node; if (l->var_type.base == TYPE_STRING && l->val.str_val) { free(l->val.str_val); } break; }
    case NODE_INC_DEC: { IncDecNode *id = (IncDecNode*)node; if (id->name) free(id->name); free_ast(id->index); break; }
    default: break;
  }
  free(node);
}
