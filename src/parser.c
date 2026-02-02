#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Token current_token = {TOKEN_UNKNOWN, NULL, 0, 0.0};

void safe_free_current_token() {
    if (current_token.text) {
        free(current_token.text);
        current_token.text = NULL;
    }
}

void eat(Lexer *l, TokenType type) {
    if (current_token.type == type) {
        safe_free_current_token();
        current_token = lexer_next(l);
    } else {
        fprintf(stderr, "Parser Error: Unexpected token %d, expected %d at %d\n", current_token.type, type, l->pos);
        safe_free_current_token();
        exit(1);
    }
}

// --- FORWARD DECLARATIONS ---
ASTNode* parse_loop(Lexer *l);
ASTNode* parse_if(Lexer *l);
ASTNode* parse_assignment_or_call(Lexer *l);
ASTNode* parse_var_decl_internal(Lexer *l, TokenType type_token);
ASTNode* parse_return(Lexer *l);
ASTNode* parse_statements(Lexer *l);

// --- EXPRESSION PARSER ---

ASTNode* parse_expression(Lexer *l);

// Helper for function calls inside expressions: identifier(arg1, arg2)
ASTNode* parse_call(Lexer *l, char *name) {
    eat(l, TOKEN_LPAREN);
    
    ASTNode *args_head = NULL;
    ASTNode **curr_arg = &args_head;
    
    if (current_token.type != TOKEN_RPAREN) {
        *curr_arg = parse_expression(l);
        curr_arg = &(*curr_arg)->next;
        
        while (current_token.type == TOKEN_COMMA) {
            eat(l, TOKEN_COMMA);
            *curr_arg = parse_expression(l);
            curr_arg = &(*curr_arg)->next;
        }
    }
    
    eat(l, TOKEN_RPAREN);
    
    CallNode *node = calloc(1, sizeof(CallNode));
    node->base.type = NODE_CALL;
    node->name = name;
    node->args = args_head;
    return (ASTNode*)node;
}

ASTNode* parse_factor(Lexer *l) {
    if (current_token.type == TOKEN_NUMBER) {
        LiteralNode *node = calloc(1, sizeof(LiteralNode));
        node->base.type = NODE_LITERAL;
        node->var_type = VAR_INT;
        node->val.int_val = current_token.int_val;
        eat(l, TOKEN_NUMBER);
        return (ASTNode*)node;
    } 
    else if (current_token.type == TOKEN_FLOAT) {
        LiteralNode *node = calloc(1, sizeof(LiteralNode));
        node->base.type = NODE_LITERAL;
        node->var_type = VAR_DOUBLE;
        node->val.double_val = current_token.double_val;
        eat(l, TOKEN_FLOAT);
        return (ASTNode*)node;
    }
    else if (current_token.type == TOKEN_STRING) {
        // Handle String as a Literal, not a VarRef
        LiteralNode *node = calloc(1, sizeof(LiteralNode));
        node->base.type = NODE_LITERAL;
        node->var_type = VAR_STRING;
        node->val.str_val = current_token.text;
        current_token.text = NULL; // Take ownership
        eat(l, TOKEN_STRING);
        return (ASTNode*)node;
    }
    else if (current_token.type == TOKEN_TRUE || current_token.type == TOKEN_FALSE) {
        LiteralNode *node = calloc(1, sizeof(LiteralNode));
        node->base.type = NODE_LITERAL;
        node->var_type = VAR_BOOL;
        node->val.int_val = (current_token.type == TOKEN_TRUE) ? 1 : 0;
        eat(l, current_token.type);
        return (ASTNode*)node;
    }
    else if (current_token.type == TOKEN_IDENTIFIER) {
        char *name = current_token.text;
        current_token.text = NULL;
        eat(l, TOKEN_IDENTIFIER);
        
        // Check if function call
        if (current_token.type == TOKEN_LPAREN) {
            return parse_call(l, name);
        }
        
        VarRefNode *node = calloc(1, sizeof(VarRefNode));
        node->base.type = NODE_VAR_REF;
        node->name = name;
        return (ASTNode*)node;
    }
    else if (current_token.type == TOKEN_LPAREN) {
        eat(l, TOKEN_LPAREN);
        ASTNode *expr = parse_expression(l);
        eat(l, TOKEN_RPAREN);
        return expr;
    } 
    else {
        fprintf(stderr, "Parser Error: Unexpected token in expression: %d\n", current_token.type);
        exit(1);
    }
}

ASTNode* parse_unary(Lexer *l) {
    if (current_token.type == TOKEN_NOT || current_token.type == TOKEN_MINUS) {
        int op = current_token.type;
        eat(l, op);
        ASTNode *operand = parse_unary(l);
        
        UnaryOpNode *node = calloc(1, sizeof(UnaryOpNode));
        node->base.type = NODE_UNARY_OP;
        node->op = op;
        node->operand = operand;
        return (ASTNode*)node;
    }
    return parse_factor(l);
}

// ... Binary Op Parsers ...
ASTNode* parse_binary_op(Lexer *l, ASTNode* (*sub_parser)(Lexer*), TokenType* ops, int num_ops) {
    ASTNode *left = sub_parser(l);
    while (1) {
        int found = 0;
        for (int i = 0; i < num_ops; i++) {
            if (current_token.type == ops[i]) {
                found = 1;
                TokenType op = current_token.type;
                eat(l, op);
                ASTNode *right = sub_parser(l);
                
                BinaryOpNode *node = calloc(1, sizeof(BinaryOpNode));
                node->base.type = NODE_BINARY_OP;
                node->op = op;
                node->left = left;
                node->right = right;
                left = (ASTNode*)node;
                break;
            }
        }
        if (!found) break;
    }
    return left;
}

ASTNode* parse_term(Lexer *l) {
    TokenType ops[] = {TOKEN_STAR, TOKEN_SLASH};
    return parse_binary_op(l, parse_unary, ops, 2);
}
ASTNode* parse_additive(Lexer *l) {
    TokenType ops[] = {TOKEN_PLUS, TOKEN_MINUS};
    return parse_binary_op(l, parse_term, ops, 2);
}
ASTNode* parse_shift(Lexer *l) {
    TokenType ops[] = {TOKEN_LSHIFT, TOKEN_RSHIFT};
    return parse_binary_op(l, parse_additive, ops, 2);
}
ASTNode* parse_relational(Lexer *l) {
    TokenType ops[] = {TOKEN_LT, TOKEN_GT, TOKEN_LTE, TOKEN_GTE};
    return parse_binary_op(l, parse_shift, ops, 4);
}
ASTNode* parse_equality(Lexer *l) {
    TokenType ops[] = {TOKEN_EQ, TOKEN_NEQ};
    return parse_binary_op(l, parse_relational, ops, 2);
}
ASTNode* parse_bitwise(Lexer *l) {
    TokenType ops[] = {TOKEN_XOR};
    return parse_binary_op(l, parse_equality, ops, 1);
}
ASTNode* parse_expression(Lexer *l) {
    return parse_bitwise(l);
}

// --- STATEMENT PARSER ---

VarType get_type_from_token(TokenType t);

// Return Statement
ASTNode* parse_return(Lexer *l) {
    eat(l, TOKEN_RETURN);
    ASTNode *val = NULL;
    if (current_token.type != TOKEN_SEMICOLON) {
        val = parse_expression(l);
    }
    eat(l, TOKEN_SEMICOLON);
    
    ReturnNode *node = calloc(1, sizeof(ReturnNode));
    node->base.type = NODE_RETURN;
    node->value = val;
    return (ASTNode*)node;
}

ASTNode* parse_assignment_or_call(Lexer *l) {
    char *name = current_token.text;
    current_token.text = NULL;
    eat(l, TOKEN_IDENTIFIER);
    
    // Case 1: Standard Function Call with Parentheses: name(...)
    if (current_token.type == TOKEN_LPAREN) {
        ASTNode *call = parse_call(l, name);
        eat(l, TOKEN_SEMICOLON);
        return call;
    }
    
    // Case 2: Assignment: name = expr;
    if (current_token.type == TOKEN_ASSIGN) {
        eat(l, TOKEN_ASSIGN);
        ASTNode *expr = parse_expression(l);
        eat(l, TOKEN_SEMICOLON);

        AssignNode *node = calloc(1, sizeof(AssignNode));
        node->base.type = NODE_ASSIGN;
        node->name = name;
        node->value = expr;
        return (ASTNode*)node;
    }
    
    // Case 3: Command-style Function Call: name arg1, arg2;
    // If it's not '(' and not '=', assume it's a call without brackets
    CallNode *node = calloc(1, sizeof(CallNode));
    node->base.type = NODE_CALL;
    node->name = name;
    node->args = NULL;

    if (current_token.type != TOKEN_SEMICOLON) {
        ASTNode *args_head = NULL;
        ASTNode **curr_arg = &args_head;

        // Parse first argument
        *curr_arg = parse_expression(l);
        curr_arg = &(*curr_arg)->next;

        // Parse subsequent arguments
        while (current_token.type == TOKEN_COMMA) {
            eat(l, TOKEN_COMMA);
            *curr_arg = parse_expression(l);
            curr_arg = &(*curr_arg)->next;
        }
        node->args = args_head;
    }

    eat(l, TOKEN_SEMICOLON);
    return (ASTNode*)node;
}

ASTNode* parse_var_decl_internal(Lexer *l, TokenType type_token) {
    VarType vtype = get_type_from_token(type_token);
    eat(l, type_token);

    if (current_token.type != TOKEN_IDENTIFIER) { fprintf(stderr, "Expected variable name\n"); exit(1); }
    char *name = current_token.text;
    current_token.text = NULL;
    eat(l, TOKEN_IDENTIFIER);

    // Note: Function Definitions are handled at the top level in parse_statements or parse_program loop usually.
    // But since we are reusing this for generic statements, assume standard var decl here.
    
    VarDeclNode *node = calloc(1, sizeof(VarDeclNode));
    node->base.type = NODE_VAR_DECL;
    node->var_type = vtype;
    node->name = name;

    if (current_token.type == TOKEN_ASSIGN) {
        eat(l, TOKEN_ASSIGN);
        node->initializer = parse_expression(l);
    } else {
        fprintf(stderr, "Error: Variables must be initialized\n");
        exit(1);
    }

    eat(l, TOKEN_SEMICOLON);
    return (ASTNode*)node;
}

// Helper to parse a single statement or a block
ASTNode* parse_single_statement_or_block(Lexer *l) {
    if (current_token.type == TOKEN_LBRACE) {
        eat(l, TOKEN_LBRACE);
        ASTNode *block = parse_statements(l);
        eat(l, TOKEN_RBRACE);
        return block;
    }
    
    // Parse single statement
    if (current_token.type == TOKEN_LOOP) {
        return parse_loop(l);
    } 
    else if (current_token.type == TOKEN_IF) {
        return parse_if(l);
    }
    else if (current_token.type == TOKEN_RETURN) {
        return parse_return(l);
    }
    else if (get_type_from_token(current_token.type) != -1) {
        return parse_var_decl_internal(l, current_token.type);
    }
    else if (current_token.type == TOKEN_IDENTIFIER) {
        return parse_assignment_or_call(l);
    }
    else if (current_token.type == TOKEN_SEMICOLON) {
        eat(l, TOKEN_SEMICOLON);
        return NULL; // Empty statement
    }
    
    fprintf(stderr, "Parser Error: Expected statement, found %d\n", current_token.type);
    exit(1);
}

ASTNode* parse_loop(Lexer *l) {
    eat(l, TOKEN_LOOP);
    ASTNode *expr = parse_expression(l);
    
    // Optional brackets handled here
    LoopNode *node = calloc(1, sizeof(LoopNode));
    node->base.type = NODE_LOOP;
    node->iterations = expr;
    node->body = parse_single_statement_or_block(l);
    return (ASTNode*)node;
}

ASTNode* parse_if(Lexer *l) {
    eat(l, TOKEN_IF);
    ASTNode *cond = parse_expression(l);
    
    ASTNode *then_body = parse_single_statement_or_block(l);
    
    ASTNode *else_body = NULL;
    if (current_token.type == TOKEN_ELIF) {
        current_token.type = TOKEN_IF; 
        else_body = parse_if(l);
    } else if (current_token.type == TOKEN_ELSE) {
        eat(l, TOKEN_ELSE);
        else_body = parse_single_statement_or_block(l);
    }

    IfNode *node = calloc(1, sizeof(IfNode));
    node->base.type = NODE_IF;
    node->condition = cond;
    node->then_body = then_body;
    node->else_body = else_body;
    return (ASTNode*)node;
}

VarType get_type_from_token(TokenType t) {
    switch(t) {
        case TOKEN_KW_INT: return VAR_INT;
        case TOKEN_KW_CHAR: return VAR_CHAR;
        case TOKEN_KW_BOOL: return VAR_BOOL;
        case TOKEN_KW_SINGLE: return VAR_FLOAT;
        case TOKEN_KW_DOUBLE: return VAR_DOUBLE;
        case TOKEN_KW_VOID: return VAR_VOID;
        default: return -1;
    }
}

// Top level decl (Func or Var)
ASTNode* parse_top_level(Lexer *l, TokenType type_token) {
    VarType vtype = get_type_from_token(type_token);
    eat(l, type_token);

    if (current_token.type != TOKEN_IDENTIFIER) { fprintf(stderr, "Expected identifier\n"); exit(1); }
    char *name = current_token.text;
    current_token.text = NULL;
    eat(l, TOKEN_IDENTIFIER);

    // Func Def
    if (current_token.type == TOKEN_LPAREN) {
        eat(l, TOKEN_LPAREN);
        Parameter *params_head = NULL;
        Parameter **curr_param = &params_head;
        
        if (current_token.type != TOKEN_RPAREN) {
            while (1) {
                TokenType pt = current_token.type;
                VarType ptype = get_type_from_token(pt);
                if ((int)ptype == -1) { fprintf(stderr, "Expected param type\n"); exit(1); }
                eat(l, pt);
                
                if (current_token.type != TOKEN_IDENTIFIER) { fprintf(stderr, "Expected param name\n"); exit(1); }
                Parameter *p = calloc(1, sizeof(Parameter));
                p->type = ptype;
                p->name = current_token.text;
                current_token.text = NULL;
                eat(l, TOKEN_IDENTIFIER);
                
                *curr_param = p;
                curr_param = &p->next;
                
                if (current_token.type == TOKEN_COMMA) eat(l, TOKEN_COMMA);
                else break;
            }
        }
        eat(l, TOKEN_RPAREN);
        eat(l, TOKEN_LBRACE);
        ASTNode *body = parse_statements(l);
        eat(l, TOKEN_RBRACE);
        
        FuncDefNode *node = calloc(1, sizeof(FuncDefNode));
        node->base.type = NODE_FUNC_DEF;
        node->name = name;
        node->ret_type = vtype;
        node->params = params_head;
        node->body = body;
        return (ASTNode*)node;
    }

    // Var Decl
    VarDeclNode *node = calloc(1, sizeof(VarDeclNode));
    node->base.type = NODE_VAR_DECL;
    node->var_type = vtype;
    node->name = name;
    if (current_token.type == TOKEN_ASSIGN) {
        eat(l, TOKEN_ASSIGN);
        node->initializer = parse_expression(l);
    } else { fprintf(stderr, "Vars must be initialized\n"); exit(1); }
    eat(l, TOKEN_SEMICOLON);
    return (ASTNode*)node;
}

ASTNode* parse_statements(Lexer *l) {
    ASTNode *head = NULL;
    ASTNode **current = &head;

    while (current_token.type != TOKEN_EOF && current_token.type != TOKEN_RBRACE) {
        ASTNode *stmt = NULL;
        
        if (current_token.type == TOKEN_LOOP) stmt = parse_loop(l);
        else if (current_token.type == TOKEN_IF) stmt = parse_if(l);
        else if (current_token.type == TOKEN_RETURN) stmt = parse_return(l);
        else if (get_type_from_token(current_token.type) != -1) {
            // Inside a block, var decls are valid statements
            stmt = parse_var_decl_internal(l, current_token.type);
        }
        else if (current_token.type == TOKEN_IDENTIFIER) stmt = parse_assignment_or_call(l);
        else if (current_token.type == TOKEN_SEMICOLON) { eat(l, TOKEN_SEMICOLON); continue; }
        else {
            fprintf(stderr, "Parser Error: Unknown statement token %d\n", current_token.type);
            exit(1);
        }

        if (stmt) {
            *current = stmt;
            current = &stmt->next;
        }
    }
    return head;
}

ASTNode* parse_program(Lexer *l) {
    safe_free_current_token();
    current_token = lexer_next(l);
    
    ASTNode *head = NULL;
    ASTNode **current = &head;

    while (current_token.type != TOKEN_EOF) {
        ASTNode *node = NULL;
        if (get_type_from_token(current_token.type) != -1) {
             node = parse_top_level(l, current_token.type);
        } 
        else if (current_token.type == TOKEN_IDENTIFIER) {
             node = parse_assignment_or_call(l); 
        } 
        else if (current_token.type == TOKEN_LOOP) {
             node = parse_loop(l);
        } 
        else if (current_token.type == TOKEN_IF) {
             node = parse_if(l);
        } 
        else if (current_token.type == TOKEN_SEMICOLON) {
             eat(l, TOKEN_SEMICOLON);
             continue;
        } 
        else {
             // Avoid infinite loop on error
             fprintf(stderr, "Unexpected top-level token %d\n", current_token.type);
             exit(1);
        }
        
        if (node) {
            *current = node;
            current = &node->next;
        } else {
            // This case should be covered by checks above or SEMICOLON continue,
            // but just in case logic slips through.
             fprintf(stderr, "Unexpected error: Null node generated\n");
             exit(1);
        }
    }
    
    safe_free_current_token();
    return head;
}

void free_ast(ASTNode *node) {
    if (!node) return;
    if (node->next) free_ast(node->next);
    
    if (node->type == NODE_FUNC_DEF) {
        FuncDefNode *f = (FuncDefNode*)node;
        free(f->name);
        free_ast(f->body);
    }
    else if (node->type == NODE_CALL) {
        CallNode *c = (CallNode*)node;
        free(c->name);
        free_ast(c->args);
    }
    else if (node->type == NODE_VAR_DECL) {
        free(((VarDeclNode*)node)->name);
        free_ast(((VarDeclNode*)node)->initializer);
    } else if (node->type == NODE_ASSIGN) {
        free(((AssignNode*)node)->name);
        free_ast(((AssignNode*)node)->value);
    } else if (node->type == NODE_VAR_REF) {
        free(((VarRefNode*)node)->name);
    } else if (node->type == NODE_BINARY_OP) {
        free_ast(((BinaryOpNode*)node)->left);
        free_ast(((BinaryOpNode*)node)->right);
    } else if (node->type == NODE_UNARY_OP) {
        free_ast(((UnaryOpNode*)node)->operand);
    } else if (node->type == NODE_LOOP) {
        free_ast(((LoopNode*)node)->iterations);
        free_ast(((LoopNode*)node)->body);
    } else if (node->type == NODE_IF) {
        free_ast(((IfNode*)node)->condition);
        free_ast(((IfNode*)node)->then_body);
        free_ast(((IfNode*)node)->else_body);
    } else if (node->type == NODE_LITERAL) {
        LiteralNode *l = (LiteralNode*)node;
        if (l->var_type == VAR_STRING && l->val.str_val) {
            free(l->val.str_val);
        }
    }
    free(node);
}
