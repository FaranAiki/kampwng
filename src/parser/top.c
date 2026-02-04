#include "parser_internal.h"
#include <string.h>
#include <stdlib.h>

ASTNode* parse_top_level(Lexer *l) {
  // 0. LINK
  if (current_token.type == TOKEN_LINK) {
    eat(l, TOKEN_LINK);
    char *lib_name = NULL;
    if (current_token.type == TOKEN_IDENTIFIER || current_token.type == TOKEN_STRING) {
      lib_name = current_token.text;
      current_token.text = NULL;
      if (current_token.type == TOKEN_IDENTIFIER) eat(l, TOKEN_IDENTIFIER);
      else eat(l, TOKEN_STRING);
    } else {
      parser_fail("Expected library name after link");
    }
    
    if (current_token.type == TOKEN_SEMICOLON) eat(l, TOKEN_SEMICOLON);

    LinkNode *node = calloc(1, sizeof(LinkNode));
    node->base.type = NODE_LINK;
    node->lib_name = lib_name;
    return (ASTNode*)node;
  }

  // 1. IMPORT
  if (current_token.type == TOKEN_IMPORT) {
    eat(l, TOKEN_IMPORT);
    if (current_token.type != TOKEN_STRING) {
      parser_fail("Expected string after import");
    }
    char* fname = current_token.text;
    current_token.text = NULL;
    eat(l, TOKEN_STRING);
    eat(l, TOKEN_SEMICOLON);

    char* src = read_import_file(fname);
    if (!src) {
      fprintf(stderr, "Could not open import file: %s\n", fname);
      free(fname);
      parser_fail("Import error");
    }
    free(fname);

    // --- SAVE CONTEXT ---
    // Save the current token (which is the token AFTER the semicolon in the main file)
    Token saved_token = current_token;
    // Nullify text to prevent recursive parse_program from freeing it
    current_token.text = NULL;
    current_token.type = TOKEN_UNKNOWN; 

    Lexer import_l;
    lexer_init(&import_l, src);
    ASTNode* imported_root = parse_program(&import_l);
    
    free(src);

    // --- RESTORE CONTEXT ---
    current_token = saved_token;

    return imported_root; 
  }
  
  // 2. EXTERN (FFI)
  if (current_token.type == TOKEN_EXTERN) {
    eat(l, TOKEN_EXTERN);
    
    VarType ret_type = get_type_from_token(current_token.type);
    if ((int)ret_type == -1) { parser_fail("Expected return type for extern"); }
    eat(l, current_token.type);

    if (current_token.type != TOKEN_IDENTIFIER) { parser_fail("Expected extern function name"); }
    char *name = current_token.text;
    current_token.text = NULL;
    eat(l, TOKEN_IDENTIFIER);

    eat(l, TOKEN_LPAREN);
    Parameter *params_head = NULL;
    Parameter **curr_param = &params_head;
    int is_varargs = 0;

    if (current_token.type != TOKEN_RPAREN) {
      while (1) {
        if (current_token.type == TOKEN_ELLIPSIS) {
          eat(l, TOKEN_ELLIPSIS);
          is_varargs = 1;
          break; 
        }

        VarType ptype = get_type_from_token(current_token.type);
        if ((int)ptype == -1) { parser_fail("Expected param type"); }
        eat(l, current_token.type);

        char *pname = NULL;
        if (current_token.type == TOKEN_IDENTIFIER) {
           pname = current_token.text;
           current_token.text = NULL;
           eat(l, TOKEN_IDENTIFIER);
        }
        
        Parameter *p = calloc(1, sizeof(Parameter));
        p->type = ptype; p->name = pname;
        *curr_param = p; curr_param = &p->next;
        
        if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA); else break;
      }
    }
    eat(l, TOKEN_RPAREN);
    eat(l, TOKEN_SEMICOLON);

    FuncDefNode *node = calloc(1, sizeof(FuncDefNode));
    node->base.type = NODE_FUNC_DEF;
    node->name = name; node->ret_type = ret_type;
    node->params = params_head; node->body = NULL; 
    node->is_varargs = is_varargs;
    return (ASTNode*)node;
  }

  if (current_token.type == TOKEN_KW_MUT || current_token.type == TOKEN_KW_IMUT) {
    return parse_var_decl_internal(l);
  }

  VarType vtype = get_type_from_token(current_token.type);
  
  // If it's NOT a type, it might be a statement/expression (REPL/Script mode)
  if ((int)vtype == -1) {
    return parse_single_statement_or_block(l);
  }

  eat(l, current_token.type); 
  
  if (current_token.type != TOKEN_IDENTIFIER) { 
    parser_fail("Expected identifier");
  }
  char *name = current_token.text;
  current_token.text = NULL;
  eat(l, TOKEN_IDENTIFIER);
  
  if (current_token.type == TOKEN_LPAREN) {
    // Function Definition
    eat(l, TOKEN_LPAREN);
    Parameter *params_head = NULL;
    Parameter **curr_param = &params_head;
    if (current_token.type != TOKEN_RPAREN) {
      while (1) {
        VarType ptype = get_type_from_token(current_token.type);
        eat(l, current_token.type);
        char *pname = current_token.text;
        current_token.text = NULL;
        eat(l, TOKEN_IDENTIFIER);
        
        Parameter *p = calloc(1, sizeof(Parameter));
        p->type = ptype; p->name = pname;
        *curr_param = p; curr_param = &p->next;
        
        if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA); else break;
      }
    }
    eat(l, TOKEN_RPAREN);
    eat(l, TOKEN_LBRACE);
    ASTNode *body = parse_statements(l);
    eat(l, TOKEN_RBRACE);
    
    FuncDefNode *node = calloc(1, sizeof(FuncDefNode));
    node->base.type = NODE_FUNC_DEF;
    node->name = name; node->ret_type = vtype;
    node->params = params_head; node->body = body;
    return (ASTNode*)node;
  } else {
    // Var Decl Fallback
    char *name_val = name;
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
       if (vtype == VAR_AUTO) { 
         parser_fail("Init required for 'let'");
       }
    }
    eat(l, TOKEN_SEMICOLON);
    VarDeclNode *node = calloc(1, sizeof(VarDeclNode));
    node->base.type = NODE_VAR_DECL;
    node->var_type = vtype; node->name = name_val;
    node->initializer = init; node->is_mutable = 1; 
    node->is_array = is_array; node->array_size = array_size;
    return (ASTNode*)node;
  }
}

ASTNode* parse_program(Lexer *l) {
  safe_free_current_token();
  current_token = lexer_next(l);
  
  ASTNode *head = NULL;
  ASTNode **current = &head;

  while (current_token.type != TOKEN_EOF) {
  ASTNode *node = parse_top_level(l);
  if (node) {
    *current = node;
    while ((*current)->next) {
      current = &(*current)->next;
    }
    current = &(*current)->next;
  }
  }
  
  safe_free_current_token();
  return head;
}
