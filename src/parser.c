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
        fprintf(stderr, "Parser Error: Unexpected token %d, expected %d\n", current_token.type, type);
        safe_free_current_token();
        exit(1);
    }
}

// --- EXPRESSION PARSER ---

ASTNode* parse_expression(Lexer *l);

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
    else if (current_token.type == TOKEN_TRUE || current_token.type == TOKEN_FALSE) {
        LiteralNode *node = calloc(1, sizeof(LiteralNode));
        node->base.type = NODE_LITERAL;
        node->var_type = VAR_BOOL;
        node->val.int_val = (current_token.type == TOKEN_TRUE) ? 1 : 0;
        eat(l, current_token.type);
        return (ASTNode*)node;
    }
    else if (current_token.type == TOKEN_IDENTIFIER) {
        VarRefNode *node = calloc(1, sizeof(VarRefNode));
        node->base.type = NODE_VAR_REF;
        node->name = current_token.text;
        current_token.text = NULL; 
        eat(l, TOKEN_IDENTIFIER);
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

// Generic binary op helper
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
    return parse_binary_op(l, parse_factor, ops, 2);
}

ASTNode* parse_additive(Lexer *l) {
    TokenType ops[] = {TOKEN_PLUS, TOKEN_MINUS};
    return parse_binary_op(l, parse_term, ops, 2);
}

ASTNode* parse_shift(Lexer *l) {
    TokenType ops[] = {TOKEN_LSHIFT, TOKEN_RSHIFT};
    return parse_binary_op(l, parse_additive, ops, 2);
}

// Relational: <, >, <=, >=
ASTNode* parse_relational(Lexer *l) {
    TokenType ops[] = {TOKEN_LT, TOKEN_GT, TOKEN_LTE, TOKEN_GTE};
    return parse_binary_op(l, parse_shift, ops, 4);
}

// Equality: ==, !=
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

ASTNode* parse_statements(Lexer *l);

ASTNode* parse_print(Lexer *l) {
    eat(l, TOKEN_PRINT);
    PrintNode *node = calloc(1, sizeof(PrintNode));
    node->base.type = NODE_PRINT;
    
    if (current_token.type == TOKEN_STRING) {
        node->message = current_token.text;
        current_token.text = NULL;
        eat(l, TOKEN_STRING);
    } else {
        fprintf(stderr, "Error: Expected string after print\n");
        exit(1);
    }
    eat(l, TOKEN_SEMICOLON);
    return (ASTNode*)node;
}

ASTNode* parse_loop(Lexer *l) {
    eat(l, TOKEN_LOOP);
    eat(l, TOKEN_LBRACKET);
    ASTNode *expr = parse_expression(l);
    eat(l, TOKEN_RBRACKET);
    eat(l, TOKEN_LBRACE);
    LoopNode *node = calloc(1, sizeof(LoopNode));
    node->base.type = NODE_LOOP;
    node->iterations = expr;
    node->body = parse_statements(l);
    eat(l, TOKEN_RBRACE);
    return (ASTNode*)node;
}

ASTNode* parse_if(Lexer *l) {
    eat(l, TOKEN_IF);
    ASTNode *cond = parse_expression(l);
    eat(l, TOKEN_LBRACE);
    ASTNode *then_body = parse_statements(l);
    eat(l, TOKEN_RBRACE);
    
    ASTNode *else_body = NULL;
    
    if (current_token.type == TOKEN_ELIF) {
        // Recursively handle elif as a nested if inside the else block
        // Consuming ELIF by converting it to IF logic virtually
        // We act as if we just saw "else { if ... }"
        current_token.type = TOKEN_IF; // Hack: Reuse parse_if for the elif part
        else_body = parse_if(l);
    } else if (current_token.type == TOKEN_ELSE) {
        eat(l, TOKEN_ELSE);
        eat(l, TOKEN_LBRACE);
        else_body = parse_statements(l);
        eat(l, TOKEN_RBRACE);
    }

    IfNode *node = calloc(1, sizeof(IfNode));
    node->base.type = NODE_IF;
    node->condition = cond;
    node->then_body = then_body;
    node->else_body = else_body;
    return (ASTNode*)node;
}

ASTNode* parse_var_decl(Lexer *l, TokenType type_token) {
    VarType vtype = VAR_INT;
    switch(type_token) {
        case TOKEN_KW_INT: vtype = VAR_INT; break;
        case TOKEN_KW_CHAR: vtype = VAR_CHAR; break;
        case TOKEN_KW_BOOL: vtype = VAR_BOOL; break;
        case TOKEN_KW_SINGLE: vtype = VAR_FLOAT; break;
        case TOKEN_KW_DOUBLE: vtype = VAR_DOUBLE; break;
        default: break;
    }
    eat(l, type_token);

    if (current_token.type != TOKEN_IDENTIFIER) { fprintf(stderr, "Expected variable name\n"); exit(1); }
    
    VarDeclNode *node = calloc(1, sizeof(VarDeclNode));
    node->base.type = NODE_VAR_DECL;
    node->var_type = vtype;
    node->name = current_token.text;
    current_token.text = NULL;
    eat(l, TOKEN_IDENTIFIER);

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

ASTNode* parse_statements(Lexer *l) {
    ASTNode *head = NULL;
    ASTNode **current = &head;

    while (current_token.type != TOKEN_EOF && current_token.type != TOKEN_RBRACE) {
        ASTNode *stmt = NULL;
        
        if (current_token.type == TOKEN_PRINT) stmt = parse_print(l);
        else if (current_token.type == TOKEN_LOOP) stmt = parse_loop(l);
        else if (current_token.type == TOKEN_IF) stmt = parse_if(l);
        else if (current_token.type == TOKEN_KW_INT || current_token.type == TOKEN_KW_CHAR ||
                   current_token.type == TOKEN_KW_BOOL || current_token.type == TOKEN_KW_SINGLE ||
                   current_token.type == TOKEN_KW_DOUBLE) {
            stmt = parse_var_decl(l, current_token.type);
        } else if (current_token.type == TOKEN_SEMICOLON) {
            eat(l, TOKEN_SEMICOLON);
            continue;
        } else {
            fprintf(stderr, "Parser Error: Unknown token type %d\n", current_token.type);
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
    ASTNode *root = parse_statements(l);
    safe_free_current_token();
    return root;
}

void free_ast(ASTNode *node) {
    if (!node) return;
    if (node->next) free_ast(node->next);
    
    if (node->type == NODE_VAR_DECL) {
        free(((VarDeclNode*)node)->name);
        free_ast(((VarDeclNode*)node)->initializer);
    } else if (node->type == NODE_VAR_REF) {
        free(((VarRefNode*)node)->name);
    } else if (node->type == NODE_BINARY_OP) {
        free_ast(((BinaryOpNode*)node)->left);
        free_ast(((BinaryOpNode*)node)->right);
    } else if (node->type == NODE_LOOP) {
        free_ast(((LoopNode*)node)->iterations);
        free_ast(((LoopNode*)node)->body);
    } else if (node->type == NODE_IF) {
        free_ast(((IfNode*)node)->condition);
        free_ast(((IfNode*)node)->then_body);
        free_ast(((IfNode*)node)->else_body);
    } else if (node->type == NODE_PRINT) {
        free(((PrintNode*)node)->message);
    }
    free(node);
}
