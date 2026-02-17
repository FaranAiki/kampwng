#include "parser_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void parser_init(Parser *p, Lexer *l) {
    if (!p) return;
    p->l = l;
    p->ctx = l ? l->ctx : NULL;
    p->recover_buf = NULL;
    
    // Initialize lists
    p->macro_head = NULL;
    p->type_head = NULL;
    p->alias_head = NULL;
    p->expansion_head = NULL;
    
    // Prime the first token
    if (l) {
        // We can't safely call lexer_next here if we want to follow the old flow strictly,
        // but typically init implies ready-to-start. 
        // Note: parse_program usually fetches the first token.
        // We'll set it to unknown so parse_program knows to fetch.
        p->current_token.type = TOKEN_UNKNOWN;
    }
}

// --- TYPE REGISTRY ---

void register_typename(Parser *p, const char *name, int is_enum) {
    TypeName *t = malloc(sizeof(TypeName));
    t->name = strdup(name);
    t->is_enum = is_enum;
    t->next = p->type_head;
    p->type_head = t;
}

int is_typename(Parser *p, const char *name) {
    TypeName *cur = p->type_head;
    while(cur) {
        if (strcmp(cur->name, name) == 0) return 1;
        cur = cur->next;
    }
    return 0;
}

static int get_typename_kind(Parser *p, const char *name) {
    TypeName *cur = p->type_head;
    while(cur) {
        if (strcmp(cur->name, name) == 0) return cur->is_enum ? 2 : 1;
        cur = cur->next;
    }
    return 0;
}

// --- ALIAS REGISTRY ---

void register_alias(Parser *p, const char *name, VarType target) {
    TypeAlias *curr = p->alias_head;
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
    
    a->next = p->alias_head;
    p->alias_head = a;
}

VarType* get_alias(Parser *p, const char *name) {
    TypeAlias *curr = p->alias_head;
    while(curr) {
        if (strcmp(curr->name, name) == 0) return &curr->target;
        curr = curr->next;
    }
    return NULL;
}

// --- MACROS ---

Token token_clone(Token t) {
    Token new_t = t;
    // Note: We are now using Arena, so strdup calls here should conceptually duplicate 
    // to heap if macros are long-lived across parsing stages, 
    // OR we can rely on them being in Arena.
    // For now, let's keep standard strdup since macros might manipulate text 
    // and we haven't fully switched parser internal data to arena yet (only Lexer outputs).
    if (t.text) new_t.text = strdup(t.text);
    return new_t;
}

void register_macro(Parser *p, const char *name, char **params, int param_count, Token *body, int body_len) {
    Macro *m = malloc(sizeof(Macro));
    m->name = strdup(name);
    m->params = params; 
    m->param_count = param_count;
    m->body = malloc(sizeof(Token) * body_len);
    for (int i=0; i<body_len; i++) {
        m->body[i] = token_clone(body[i]);
    }
    m->body_len = body_len;
    m->next = p->macro_head;
    p->macro_head = m;
}

static Macro* find_macro(Parser *p, const char *name) {
    Macro *curr = p->macro_head;
    while(curr) {
        if (strcmp(curr->name, name) == 0) return curr;
        curr = curr->next;
    }
    return NULL;
}

static void free_token_seq(Token *tokens, int count) {
    for(int i=0; i<count; i++) {
        if (tokens[i].text) free(tokens[i].text);
    }
    free(tokens);
}

// --- TOKEN STREAM MANAGEMENT ---

Token lexer_next_raw(Parser *p) {
    return lexer_next(p->l);
}

Token get_next_token_expanded(Parser *p) {
    if (p->expansion_head) {
        if (p->expansion_head->pos < p->expansion_head->count) {
            return token_clone(p->expansion_head->tokens[p->expansion_head->pos++]);
        } else {
            Expansion *finished = p->expansion_head;
            p->expansion_head = p->expansion_head->next;
            free_token_seq(finished->tokens, finished->count); 
            free(finished);
            return get_next_token_expanded(p);
        }
    }
    return lexer_next(p->l);
}

static Token fetch_safe(Parser *p) { return get_next_token_expanded(p); }

void parser_fail_at(Parser *p, Token t, const char *msg) {
    report_error(p->l, t, msg); // Use stored lexer for diagnostic context
    if (p->ctx) p->ctx->error_count++;
    
    // Attempt recovery
    if (p->recover_buf) {
        longjmp(*p->recover_buf, 1);
    } else {
        // Fallback if no recovery set (e.g. fatal init error)
        exit(1);
    }
}

void parser_fail(Parser *p, const char *msg) {
    parser_fail_at(p, p->current_token, msg);
}

void parser_sync(Parser *p) {
    while (p->current_token.type != TOKEN_EOF) {
        if (p->current_token.type == TOKEN_SEMICOLON) {
            eat(p, TOKEN_SEMICOLON);
            return;
        }
        if (p->current_token.type == TOKEN_RBRACE) {
            eat(p, TOKEN_RBRACE);
            return;
        }
        switch (p->current_token.type) {
            case TOKEN_CLASS:
            case TOKEN_STRUCT:
            case TOKEN_UNION: 
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
                eat(p, p->current_token.type); 
        }
    }
}

void eat(Parser *p, TokenType type) {
  if (p->current_token.type == type) {
    // Current token logic is simpler now, we don't manually free arena strings here
    // as they are lifetime-bound to context/lexer.
    
    Token t = fetch_safe(p);
    
    while (t.type == TOKEN_IDENTIFIER) {
        Macro *m = find_macro(p, t.text);
        if (!m) break; 
        
        Token **args = NULL;
        int *arg_lens = NULL;
        
        if (m->param_count > 0) {
            Token peek = fetch_safe(p);
            if (peek.type != TOKEN_LPAREN) {
                parser_fail(p, "Function-like macro requires arguments list '('.");
            }
            if(peek.text) free(peek.text);

            args = malloc(sizeof(Token*) * m->param_count);
            arg_lens = calloc(m->param_count, sizeof(int));
            
            for(int i=0; i<m->param_count; i++) {
                int cap = 8; int len = 0;
                args[i] = malloc(sizeof(Token) * cap);
                int depth = 0;
                while(1) {
                    Token arg_t = fetch_safe(p);
                    if (arg_t.type == TOKEN_EOF) parser_fail(p, "Unexpected EOF in macro arguments");
                    
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
        ex->next = p->expansion_head;
        p->expansion_head = ex;
        
        t = fetch_safe(p);
    }
    
    p->current_token = t;

  } else {
    char msg[256];
    const char *expected = get_token_description(type);
    const char *found = p->current_token.type == TOKEN_EOF ? "end of file" : 
                        (p->current_token.text ? p->current_token.text : token_type_to_string(p->current_token.type));
    
    snprintf(msg, sizeof(msg), "Expected '%s' but found '%s'", expected, found);
    parser_fail(p, msg);
  }
}

// Composite type parsing helper
VarType parse_type(Parser *p) {
  VarType t = {TYPE_UNKNOWN, 0, NULL, 0, 0, 0, NULL, NULL, 0, 0}; 

  if (p->current_token.type == TOKEN_KW_UNSIGNED) {
      t.is_unsigned = 1;
      eat(p, TOKEN_KW_UNSIGNED);
  }

  if (p->current_token.type == TOKEN_IDENTIFIER) {
      VarType *alias = get_alias(p, p->current_token.text);
      if (alias) {
          t = *alias; 
          if (t.class_name) t.class_name = strdup(t.class_name);
          eat(p, TOKEN_IDENTIFIER);
      }
      else {
          int kind = get_typename_kind(p, p->current_token.text);
          if (kind != 0) {
              if (kind == 2) { 
                  t.base = TYPE_ENUM;
                  t.class_name = strdup(p->current_token.text);
              } else {
                  t.base = TYPE_CLASS;
                  t.class_name = strdup(p->current_token.text);
              }
              eat(p, TOKEN_IDENTIFIER);
          } else {
              return t; 
          }
      }
  } else {
      TokenType ct = p->current_token.type;
      if (ct == TOKEN_KW_INT) { t.base = TYPE_INT; eat(p, TOKEN_KW_INT); }
      else if (ct == TOKEN_KW_SHORT) { t.base = TYPE_SHORT; eat(p, TOKEN_KW_SHORT); }
      else if (ct == TOKEN_KW_LONG) {
          eat(p, TOKEN_KW_LONG);
          if (p->current_token.type == TOKEN_KW_LONG) {
              eat(p, TOKEN_KW_LONG);
              if (p->current_token.type == TOKEN_KW_DOUBLE) {
                  eat(p, TOKEN_KW_DOUBLE);
                  t.base = TYPE_LONG_DOUBLE;
              } else {
                  t.base = TYPE_LONG_LONG;
              }
          } else if (p->current_token.type == TOKEN_KW_DOUBLE) {
              eat(p, TOKEN_KW_DOUBLE);
              t.base = TYPE_LONG_DOUBLE;
          } else if (p->current_token.type == TOKEN_KW_INT) {
              eat(p, TOKEN_KW_INT);
              t.base = TYPE_LONG;
          } else {
              t.base = TYPE_LONG;
          }
      }
      else if (ct == TOKEN_KW_DOUBLE) {
          eat(p, TOKEN_KW_DOUBLE);
          if (p->current_token.type == TOKEN_KW_LONG) {
              eat(p, TOKEN_KW_LONG);
              if (p->current_token.type == TOKEN_KW_LONG) eat(p, TOKEN_KW_LONG); 
              t.base = TYPE_LONG_DOUBLE;
          } else {
              t.base = TYPE_DOUBLE;
          }
      }
      else if (ct == TOKEN_KW_CHAR) { t.base = TYPE_CHAR; eat(p, TOKEN_KW_CHAR); }
      else if (ct == TOKEN_KW_BOOL) { t.base = TYPE_BOOL; eat(p, TOKEN_KW_BOOL); }
      else if (ct == TOKEN_KW_SINGLE) { t.base = TYPE_FLOAT; eat(p, TOKEN_KW_SINGLE); }
      else if (ct == TOKEN_KW_STRING) { t.base = TYPE_STRING; eat(p, TOKEN_KW_STRING); }
      else if (ct == TOKEN_KW_VOID) { t.base = TYPE_VOID; eat(p, TOKEN_KW_VOID); }
      else if (ct == TOKEN_KW_LET) { t.base = TYPE_AUTO; eat(p, TOKEN_KW_LET); }
      else {
          if (t.is_unsigned) t.base = TYPE_INT; 
          else return t; 
      }
  }

  while (p->current_token.type == TOKEN_STAR) {
    t.ptr_depth++;
    eat(p, TOKEN_STAR);
  }
  
  return t;
}

VarType parse_func_ptr_decl(Parser *p, VarType ret_type, char **out_name) {
    VarType vt = {0};
    vt.is_func_ptr = 1;
    vt.fp_ret_type = malloc(sizeof(VarType));
    *vt.fp_ret_type = ret_type;
    
    eat(p, TOKEN_LPAREN);
    eat(p, TOKEN_STAR);
    
    if (p->current_token.type != TOKEN_IDENTIFIER) {
        parser_fail(p, "Expected identifier in function pointer declaration");
    }
    
    if (out_name) *out_name = strdup(p->current_token.text);
    eat(p, TOKEN_IDENTIFIER);
    
    eat(p, TOKEN_RPAREN);
    eat(p, TOKEN_LPAREN);
    
    int cap = 4;
    vt.fp_param_types = malloc(sizeof(VarType) * cap);
    vt.fp_param_count = 0;
    
    if (p->current_token.type != TOKEN_RPAREN) {
        while(1) {
            if (p->current_token.type == TOKEN_ELLIPSIS) {
                vt.fp_is_varargs = 1;
                eat(p, TOKEN_ELLIPSIS);
                break;
            }
            
            VarType pt = parse_type(p);
            if (pt.base == TYPE_UNKNOWN) parser_fail(p, "Expected type in function pointer params");
            
            if (p->current_token.type == TOKEN_IDENTIFIER) {
                eat(p, TOKEN_IDENTIFIER); 
            }
            
             if (p->current_token.type == TOKEN_LBRACKET) {
                eat(p, TOKEN_LBRACKET);
                if (p->current_token.type != TOKEN_RBRACKET) {
                     ASTNode* tmp = parse_expression(p);
                     free_ast(tmp);
                }
                eat(p, TOKEN_RBRACKET);
                pt.ptr_depth++;
            }
            
            if (vt.fp_param_count >= cap) {
                cap *= 2;
                vt.fp_param_types = realloc(vt.fp_param_types, sizeof(VarType) * cap);
            }
            vt.fp_param_types[vt.fp_param_count++] = pt;
            
            if (p->current_token.type == TOKEN_COMMA) eat(p, TOKEN_COMMA);
            else break;
        }
    }
    eat(p, TOKEN_RPAREN);
    
    return vt;
}

static char* read_file_content(const char* path) {
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

char* read_import_file(const char* filename) {
  const char* paths[] = { "", "lib/" };
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

ASTNode* parse_program(Parser *p) {
  // Prime the first token if not already
  p->current_token = lexer_next_raw(p);
  
  ASTNode *head = NULL;
  ASTNode **current = &head;
  
  jmp_buf recover_buf;
  p->recover_buf = &recover_buf;

  while (p->current_token.type != TOKEN_EOF) {
    if (setjmp(recover_buf) != 0) {
        parser_sync(p);
        if (p->current_token.type == TOKEN_EOF) break;
    }
   
    ASTNode *node = parse_top_level(p);
    if (node) {
        if (!*current) *current = node; 
        
        ASTNode *iter = node;
        while (iter->next) iter = iter->next;
        current = &iter->next;
    }
  }
  
  p->recover_buf = NULL;
  return head;
}

// AST Utilities
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
