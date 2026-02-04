#include "parser_internal.h"
#include <string.h>
#include <stdlib.h>

// Helper to register aliases defined in 'define' statement
// Exposed from core.c via prototype hack or just implementation here if static?
// We put register_alias in core.c, need prototype
void register_alias(const char *name, VarType type);

ASTNode* parse_top_level(Lexer *l) {
  // 0. DEFINE (Type Aliases)
  if (current_token.type == TOKEN_DEFINE) {
    eat(l, TOKEN_DEFINE);
    
    // Parse list of aliases: define i64, I0 as int
    char **names = malloc(sizeof(char*) * 8); // simplified dynamic array
    int count = 0;
    int cap = 8;
    
    do {
      if (current_token.type != TOKEN_IDENTIFIER) parser_fail("Expected identifier in define");
      if (count >= cap) { cap *= 2; names = realloc(names, sizeof(char*)*cap); }
      names[count++] = strdup(current_token.text);
      eat(l, TOKEN_IDENTIFIER);
      
      if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA);
      else break;
    } while (1);
    
    eat(l, TOKEN_AS);
    
    VarType t = parse_type(l);
    if (t.base == TYPE_UNKNOWN) parser_fail("Expected type in define");
    
    // Register all
    for (int i=0; i<count; i++) {
      register_alias(names[i], t);
      free(names[i]);
    }
    free(names);
    
    // define is a directive, returns NULL (ignored in AST, processed immediately)
    // But parse_program expects ASTNode*. Return NULL is handled by caller loop.
    return NULL; 
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
      parser_fail("Expected library name after link");
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

    Token saved_token = current_token;
    current_token.text = NULL;
    current_token.type = TOKEN_UNKNOWN; 

    Lexer import_l;
    lexer_init(&import_l, src);
    ASTNode* imported_root = parse_program(&import_l);
    
    free(src);
    current_token = saved_token;

    return imported_root; 
  }
  
  // 3. EXTERN (FFI)
  if (current_token.type == TOKEN_EXTERN) {
    eat(l, TOKEN_EXTERN);
    
    VarType ret_type = parse_type(l);
    if (ret_type.base == TYPE_UNKNOWN) { parser_fail("Expected return type for extern"); }

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

        VarType ptype = parse_type(l);
        if (ptype.base == TYPE_UNKNOWN) { parser_fail("Expected param type"); }

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

  // Attempt to parse type for Var Decl or Func Def
  // We cannot easily peek ahead infinitely to distinguish "Type Name" vs "Expr".
  // Hack: Check if current token LOOKS like a type start (keyword or known alias).
  // But since we have aliases, we might confuse `MyVar = 10` with `MyType MyVar`.
  // parse_type handles identifiers by checking alias table.
  
  VarType vtype = parse_type(l);
  
  if (vtype.base == TYPE_UNKNOWN) {
    // Not a type, assume statement
    return parse_single_statement_or_block(l);
  }

  if (current_token.type != TOKEN_IDENTIFIER) { 
    parser_fail("Expected identifier after type");
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
        VarType ptype = parse_type(l);
        if (ptype.base == TYPE_UNKNOWN) parser_fail("Expected param type");
        
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
       if (vtype.base == TYPE_AUTO) { 
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
        // If head is NULL, set it. 
        if (!*current) *current = node; 
        
        // Find end
        ASTNode *iter = node;
        while (iter->next) iter = iter->next;
        current = &iter->next;
    }
  }
  
  safe_free_current_token();
  return head;
}
