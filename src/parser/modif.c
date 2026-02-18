#include "modif.h"

ASTNode* parse_define(Parser *p) {
  eat(p, TOKEN_DEFINE);

  MacroSig *sigs = NULL;
  int sig_count = 0;
  int sig_cap = 0;

  do {
      if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected macro name after 'define'");
      char *macro_name = parser_strdup(p, p->current_token.text);
      eat(p, TOKEN_IDENTIFIER);
      
      char **params = NULL;
      int param_count = 0;
      if (p->current_token.type == TOKEN_LPAREN) {
          eat(p, TOKEN_LPAREN);
          int cap = 4;
          params = parser_alloc(p, sizeof(char*) * cap);
          
          if (p->current_token.type != TOKEN_RPAREN) {
              while(1) {
                  if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected parameter name in define definition");
                  if (param_count >= cap) { 
                      cap *= 2; 
                      char **new_params = parser_alloc(p, sizeof(char*)*cap);
                      memcpy(new_params, params, sizeof(char*)*param_count);
                      params = new_params;
                  }
                  params[param_count++] = parser_strdup(p, p->current_token.text);
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
          // Simple realloc
          MacroSig *new_sigs = parser_alloc(p, sizeof(MacroSig) * sig_cap);
          if (sigs) memcpy(new_sigs, sigs, sizeof(MacroSig) * sig_count);
          sigs = new_sigs;
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
      
      report_error(p->l, p->current_token, msg);
      
      char hint[256];
      snprintf(hint, sizeof(hint), "Did you mean \"define %s as %s...\"?", sigs[0].name, found);
      report_hint(p->l, p->current_token, hint);
      
      if (p->recover_buf) longjmp(*p->recover_buf, 1);
  }
  eat(p, TOKEN_AS);
  
  Token *body_tokens = parser_alloc(p, sizeof(Token) * 32);
  int body_cap = 32;
  int body_len = 0;
  while (p->current_token.type != TOKEN_SEMICOLON && p->current_token.type != TOKEN_EOF) {
      if (body_len >= body_cap) { 
          body_cap *= 2; 
          Token *new_toks = parser_alloc(p, sizeof(Token)*body_cap);
          memcpy(new_toks, body_tokens, sizeof(Token)*body_len);
          body_tokens = new_toks;
      }
      Token t = p->current_token;
      if (t.text) t.text = parser_strdup(p, t.text);
      body_tokens[body_len++] = t;
      eat(p, p->current_token.type);
  }
  
  for(int i=0; i<sig_count; i++) {
      register_macro(p, sigs[i].name, sigs[i].params, sigs[i].param_count, body_tokens, body_len);
  }
  
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
          // No free(cb_name)
          eat(p, TOKEN_SEMICOLON);
          return NULL;
      }
      
      if (p->current_token.type == TOKEN_AS) {
          eat(p, TOKEN_AS);
      }
      
      while(1) {
          if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected new alias name after type in typedef");
          
          char *new_name = parser_strdup(p, p->current_token.text);
          eat(p, TOKEN_IDENTIFIER);
          
          VarType current_target = target;
          while (p->current_token.type == TOKEN_LBRACKET) {
              eat(p, TOKEN_LBRACKET);
              if (p->current_token.type != TOKEN_RBRACKET) {
                  ASTNode *sz = parse_expression(p);
                  if (sz && sz->type == NODE_LITERAL) current_target.array_size = ((LiteralNode*)sz)->val.int_val;
                  (void)sz;
              }
              eat(p, TOKEN_RBRACKET);
              current_target.ptr_depth++;
          }
          
          register_alias(p, new_name, current_target);
          
          if (p->current_token.type == TOKEN_COMMA) eat(p, TOKEN_COMMA);
          else break;
      }
  } else {
      char **names = NULL;
      int name_count = 0;
      int name_cap = 4;
      names = parser_alloc(p, sizeof(char*) * name_cap);
      
      while (1) {
          if (p->current_token.type != TOKEN_IDENTIFIER) parser_fail(p, "Expected new type name after 'typedef'");
          if (name_count >= name_cap) { 
              name_cap *= 2; 
              char **new_names = parser_alloc(p, sizeof(char*) * name_cap);
              memcpy(new_names, names, sizeof(char*) * name_count);
              names = new_names;
          }
          names[name_count++] = parser_strdup(p, p->current_token.text);
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
              (void)sz;
          }
          eat(p, TOKEN_RBRACKET);
          target.ptr_depth++;
      }
      
      for (int i = 0; i < name_count; i++) {
          register_alias(p, names[i], target);
      }
  }
  
  eat(p, TOKEN_SEMICOLON);
  return NULL;
}
