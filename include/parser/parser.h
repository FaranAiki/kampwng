#ifndef PARSER_H
#define PARSER_H

#include "../lexer/lexer.h"
#include "../common/debug.h"
#include "../common/context.h"
#include <setjmp.h>
#include <stdbool.h>

#include "parser/typestruct.h"
#include "semantic/typestruct.h"

typedef struct Macro Macro;
typedef struct TypeName TypeName;
typedef struct TypeAlias TypeAlias;
typedef struct Expansion Expansion;

typedef struct Parser {
    Lexer *l;
    CompilerContext *ctx;
    Token current_token;
    jmp_buf *recover_buf;
    Macro *macro_head;
    TypeName *type_head;
    TypeAlias *alias_head;
    Expansion *expansion_head;
} Parser;

void parser_init(Parser *p, Lexer *l);

ASTNode* parse_program(Parser *p);
ASTNode* parse_expression(Parser *p);

#include "emitter.h"
#include "link.h"
#include "modifier.h"

#endif
