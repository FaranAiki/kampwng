#include "link.h"

ASTNode* parse_import(Parser *p) {
  eat(p, TOKEN_IMPORT);
  if (p->current_token.type != TOKEN_STRING) parser_fail(p, "Expected file path string after 'import'");
  char* fname = parser_strdup(p, p->current_token.text);
  p->current_token.text = NULL;
  eat(p, TOKEN_STRING);
  
  // optional semicolon
  if (p->current_token.type == TOKEN_SEMICOLON) {
      eat(p, TOKEN_SEMICOLON);
  } 
  
  char* src = read_import_file(p, fname);
  if (!src) { 
      char msg[256];
      snprintf(msg, 256, "Could not open imported file: '%s'", fname);
      parser_fail(p, msg); 
  }
  
  Lexer import_l; 
  lexer_init(&import_l, p->ctx, fname, src);
  
  Parser import_p;
  parser_init(&import_p, &import_l);
  
  ASTNode* imported_root = parse_program(&import_p);
  
  return imported_root; 
}

ASTNode* parse_link(Parser *p) {
  eat(p, TOKEN_LINK);
  char *lib_name = NULL;
  if (p->current_token.type == TOKEN_IDENTIFIER || p->current_token.type == TOKEN_STRING) {
    lib_name = parser_strdup(p, p->current_token.text);
    p->current_token.text = NULL;
    if (p->current_token.type == TOKEN_IDENTIFIER) eat(p, TOKEN_IDENTIFIER);
    else eat(p, TOKEN_STRING);
  } else {
    parser_fail(p, "Expected library name (string or identifier) after 'link'");
  }
  if (p->current_token.type == TOKEN_SEMICOLON) eat(p, TOKEN_SEMICOLON);
  LinkNode *node = parser_alloc(p, sizeof(LinkNode));
  node->base.type = NODE_LINK;
  node->lib_name = lib_name;
  return (ASTNode*)node;
}

