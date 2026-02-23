#include "emitter.h"
#include "../lexer/lexer.h"
#include "../common/arena.h"
#include "../common/context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void parser_emit_indent(StringBuilder *sb, int indent) {
    for (int i = 0; i < indent; i++) sb_append(sb, "  ");
}

void parser_emit_type(StringBuilder *sb, VarType t) {
    for (int i = 0; i < t.vector_depth; i++) sb_append(sb, "vector ");
    if (t.is_unsigned) sb_append(sb, "unsigned ");
    
    switch (t.base) {
        case TYPE_INT: sb_append(sb, "int"); break;
        case TYPE_SHORT: sb_append(sb, "short"); break;
        case TYPE_LONG: sb_append(sb, "long"); break;
        case TYPE_LONG_LONG: sb_append(sb, "long long"); break;
        case TYPE_CHAR: sb_append(sb, "char"); break;
        case TYPE_BOOL: sb_append(sb, "bool"); break;
        case TYPE_FLOAT: sb_append(sb, "single"); break;
        case TYPE_DOUBLE: sb_append(sb, "double"); break;
        case TYPE_LONG_DOUBLE: sb_append(sb, "long double"); break;
        case TYPE_VOID: sb_append(sb, "void"); break;
        case TYPE_STRING: sb_append(sb, "string"); break;
        case TYPE_AUTO: sb_append(sb, "let"); break;
        case TYPE_CLASS: sb_append(sb, t.class_name ? t.class_name : "class"); break;
        default: sb_append(sb, "auto"); break;
    }

    for (int i = 0; i < t.ptr_depth; i++) sb_append(sb, "*");
    
    if (t.array_size > 0) {
        sb_append_fmt(sb, "[%d]", t.array_size);
    }
}

int needs_semicolon(ASTNode *node) {
    if (!node) return 0;
    switch (node->type) {
        case NODE_CALL:
        case NODE_METHOD_CALL:
        case NODE_ASSIGN:
        case NODE_INC_DEC:
        case NODE_UNARY_OP:
        case NODE_BINARY_OP:
        case NODE_VAR_REF:
        case NODE_ARRAY_ACCESS:
        case NODE_MEMBER_ACCESS:
        case NODE_CAST:
        case NODE_EMIT:
            return 1;
        case NODE_WASH:
            return ((WashNode*)node)->wash_type == 2;
        default:
            return 0;
    }
}

void parser_emit_block(StringBuilder *sb, ASTNode *body, int indent) {
    sb_append(sb, " {\n");
    ASTNode *curr = body;
    while (curr) {
        parser_emit_indent(sb, indent + 1);
        parser_emit_ast_node(sb, curr, indent + 1);
        if (needs_semicolon(curr)) {
            sb_append(sb, ";");
        }
        sb_append(sb, "\n");
        curr = curr->next;
    }
    parser_emit_indent(sb, indent);
    sb_append(sb, "}");
}

void parser_emit_ast_node(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;

    switch (node->type) {
        case NODE_ROOT:
            break;

        case NODE_FUNC_DEF: {
            FuncDefNode *fn = (FuncDefNode*)node;
            if (fn->is_public) sb_append(sb, "public ");
            if (fn->is_static) sb_append(sb, "static ");
            if (fn->is_open) sb_append(sb, "open ");
            if (fn->is_is_a == IS_A_FINAL) sb_append(sb, "final ");
            if (fn->is_is_a == IS_A_NAKED) sb_append(sb, "naked ");
            if (fn->is_has_a == HAS_A_INERT) sb_append(sb, "inert ");
            if (fn->is_has_a == HAS_A_REACTIVE) sb_append(sb, "reactive ");
            if (fn->is_flux) sb_append(sb, "flux ");
            if (!fn->is_pure) sb_append(sb, "impure ");
            if (!fn->is_pristine) sb_append(sb, "tainted ");
            
            parser_emit_type(sb, fn->ret_type);
            sb_append_fmt(sb, " %s(", fn->name ? fn->name : "anon");
            
            Parameter *p = fn->params;
            while (p) {
                parser_emit_type(sb, p->type);
                sb_append_fmt(sb, " %s", p->name);
                if (p->next) sb_append(sb, ", ");
                p = p->next;
            }
            if (fn->is_varargs) {
                if (fn->params) sb_append(sb, ", ");
                sb_append(sb, "...");
            }
            sb_append(sb, ")");
            if (fn->body) parser_emit_block(sb, fn->body, indent);
            else sb_append(sb, ";");
            break;
        }

        case NODE_VAR_DECL: {
            VarDeclNode *vn = (VarDeclNode*)node;
            if (vn->is_public) sb_append(sb, "public ");
            if (vn->is_static) sb_append(sb, "static ");
            if (vn->is_open) sb_append(sb, "open ");
            if (vn->is_const) sb_append(sb, "const ");
            if (vn->is_is_a == IS_A_FINAL) sb_append(sb, "final ");
            if (vn->is_is_a == IS_A_NAKED) sb_append(sb, "naked ");
            if (vn->is_has_a == HAS_A_INERT) sb_append(sb, "inert ");
            if (vn->is_has_a == HAS_A_REACTIVE) sb_append(sb, "reactive ");
            
            if (!vn->is_pure) sb_append(sb, "impure ");
            if (!vn->is_pristine) sb_append(sb, "tainted ");
            
            if (vn->is_mutable) sb_append(sb, "mut ");
            else if (!vn->is_const) sb_append(sb, "imut ");

            parser_emit_type(sb, vn->var_type);
            sb_append_fmt(sb, " %s", vn->name);
            
            if (vn->is_array) {
                sb_append(sb, "[");
                if (vn->array_size) parser_emit_ast_node(sb, vn->array_size, 0);
                sb_append(sb, "]");
            }

            if (vn->initializer) {
                sb_append(sb, " = ");
                parser_emit_ast_node(sb, vn->initializer, 0);
            }
            sb_append(sb, ";");
            break;
        }

        case NODE_NAMESPACE: {
            NamespaceNode *ns = (NamespaceNode*)node;
            sb_append_fmt(sb, "namespace %s", ns->name);
            parser_emit_block(sb, ns->body, indent);
            break;
        }

        case NODE_CLASS: {
            ClassNode *cn = (ClassNode*)node;
            if (cn->is_public) sb_append(sb, "public ");
            if (cn->is_static) sb_append(sb, "static ");
            if (cn->is_open) sb_append(sb, "open ");
            if (cn->is_is_a == IS_A_FINAL) sb_append(sb, "final ");
            if (cn->is_is_a == IS_A_NAKED) sb_append(sb, "naked ");
            if (cn->is_has_a == HAS_A_INERT) sb_append(sb, "inert ");
            if (cn->is_has_a == HAS_A_REACTIVE) sb_append(sb, "reactive ");
            if (cn->is_extern) sb_append(sb, "extern ");
            
            sb_append_fmt(sb, "%s %s", cn->is_union ? "union" : "class", cn->name);
            if (cn->parent_name) sb_append_fmt(sb, " is %s", cn->parent_name);
            
            sb_append(sb, " {\n");
            ASTNode *mem = cn->members;
            while (mem) {
                parser_emit_indent(sb, indent + 1);
                parser_emit_ast_node(sb, mem, indent + 1);
                sb_append(sb, "\n");
                mem = mem->next;
            }
            parser_emit_indent(sb, indent);
            sb_append(sb, "};\n");
            break;
        }

        case NODE_ENUM: {
            EnumNode *en = (EnumNode*)node;
            sb_append_fmt(sb, "enum %s [", en->name);
            EnumEntry *cur = en->entries;
            while(cur) {
                sb_append(sb, cur->name);
                if (cur->value != -1) sb_append_fmt(sb, " = %d", cur->value);
                if (cur->next) sb_append(sb, ", ");
                cur = cur->next;
            }
            sb_append(sb, "];");
            break;
        }

        case NODE_RETURN: {
            ReturnNode *rn = (ReturnNode*)node;
            sb_append(sb, "return");
            if (rn->value) {
                sb_append(sb, " ");
                parser_emit_ast_node(sb, rn->value, 0);
            }
            sb_append(sb, ";");
            break;
        }

        case NODE_BREAK:
            sb_append(sb, "break;");
            break;
        
        case NODE_CONTINUE:
            sb_append(sb, "continue;");
            break;

        case NODE_IF: {
            IfNode *in = (IfNode*)node;
            sb_append(sb, "if ");
            parser_emit_ast_node(sb, in->condition, 0);
            parser_emit_block(sb, in->then_body, indent);
            if (in->else_body) {
                sb_append(sb, " else ");
                if (in->else_body->type == NODE_IF) {
                    parser_emit_ast_node(sb, in->else_body, indent); 
                } else {
                    parser_emit_block(sb, in->else_body, indent);
                }
            }
            break;
        }

        case NODE_WHILE: {
            WhileNode *wn = (WhileNode*)node;
            sb_append(sb, "while ");
            if (wn->is_do_while) sb_append(sb, "once ");
            parser_emit_ast_node(sb, wn->condition, 0);
            parser_emit_block(sb, wn->body, indent);
            break;
        }

        case NODE_LOOP: {
            LoopNode *ln = (LoopNode*)node;
            sb_append(sb, "loop ");
            parser_emit_ast_node(sb, ln->iterations, 0);
            parser_emit_block(sb, ln->body, indent);
            break;
        }
        
        case NODE_FOR_IN: {
            ForInNode *fn = (ForInNode*)node;
            sb_append_fmt(sb, "for %s in ", fn->var_name);
            parser_emit_ast_node(sb, fn->collection, 0);
            parser_emit_block(sb, fn->body, indent);
            break;
        }

        case NODE_EMIT: {
            EmitNode *en = (EmitNode*)node;
            sb_append(sb, "emit ");
            parser_emit_ast_node(sb, en->value, 0);
            break;
        }

        case NODE_WASH: {
            WashNode *wn = (WashNode*)node;
            if (wn->wash_type == 2) {
                sb_append_fmt(sb, "untaint %s", wn->var_name);
            } else {
                sb_append_fmt(sb, "%s %s", wn->wash_type == 1 ? "clean" : "wash", wn->var_name);
                if (wn->err_name) {
                    sb_append_fmt(sb, " as %s", wn->err_name);
                }
                parser_emit_block(sb, wn->body, indent);
                if (wn->else_body) {
                    sb_append(sb, " else ");
                    if (wn->else_body->type == NODE_WASH || wn->else_body->type == NODE_IF) {
                        parser_emit_ast_node(sb, wn->else_body, indent);
                    } else {
                        parser_emit_block(sb, wn->else_body, indent);
                    }
                }
            }
            break;
        }

        case NODE_SWITCH: {
            SwitchNode *sn = (SwitchNode*)node;
            sb_append(sb, "switch ");
            parser_emit_ast_node(sb, sn->condition, 0);
            sb_append(sb, " {\n");
            ASTNode *c = sn->cases;
            while (c) {
                CaseNode *cn = (CaseNode*)c;
                parser_emit_indent(sb, indent + 1);
                if (cn->is_leak) sb_append(sb, "leak ");
                sb_append(sb, "case ");
                parser_emit_ast_node(sb, cn->value, 0);
                sb_append(sb, ":");
                if (cn->body) {
                    sb_append(sb, "\n");
                    ASTNode *stmt = cn->body;
                    while (stmt) {
                        parser_emit_indent(sb, indent + 2);
                        parser_emit_ast_node(sb, stmt, indent + 2);
                        if (needs_semicolon(stmt)) sb_append(sb, ";");
                        sb_append(sb, "\n");
                        stmt = stmt->next;
                    }
                } else {
                    sb_append(sb, "\n");
                }
                c = c->next;
            }
            if (sn->default_case) {
                parser_emit_indent(sb, indent + 1);
                sb_append(sb, "default:\n");
                ASTNode *stmt = sn->default_case;
                while (stmt) {
                    parser_emit_indent(sb, indent + 2);
                    parser_emit_ast_node(sb, stmt, indent + 2);
                    if (needs_semicolon(stmt)) sb_append(sb, ";");
                    sb_append(sb, "\n");
                    stmt = stmt->next;
                }
            }
            parser_emit_indent(sb, indent);
            sb_append(sb, "}");
            break;
        }

        case NODE_CALL: {
            CallNode *cn = (CallNode*)node;
            sb_append_fmt(sb, "%s(", cn->name);
            ASTNode *arg = cn->args;
            while (arg) {
                parser_emit_ast_node(sb, arg, 0);
                if (arg->next) sb_append(sb, ", ");
                arg = arg->next;
            }
            sb_append(sb, ")");
            break;
        }
        
        case NODE_METHOD_CALL: {
            MethodCallNode *mc = (MethodCallNode*)node;
            parser_emit_ast_node(sb, mc->object, 0);
            sb_append_fmt(sb, ".%s(", mc->method_name);
            ASTNode *arg = mc->args;
            while (arg) {
                parser_emit_ast_node(sb, arg, 0);
                if (arg->next) sb_append(sb, ", ");
                arg = arg->next;
            }
            sb_append(sb, ")");
            break;
        }

        case NODE_ASSIGN: {
            AssignNode *an = (AssignNode*)node;
            if (an->name) sb_append(sb, an->name);
            else parser_emit_ast_node(sb, an->target, 0);
            
            switch(an->op) {
                case TOKEN_ASSIGN: sb_append(sb, " = "); break;
                case TOKEN_PLUS_ASSIGN: sb_append(sb, " += "); break;
                case TOKEN_MINUS_ASSIGN: sb_append(sb, " -= "); break;
                default: sb_append(sb, " = "); break; 
            }
            parser_emit_ast_node(sb, an->value, 0);
            break;
        }

        case NODE_BINARY_OP: {
            BinaryOpNode *bn = (BinaryOpNode*)node;
            sb_append(sb, "(");
            parser_emit_ast_node(sb, bn->left, 0);
            
            const char *op = "?";
            switch (bn->op) {
                case TOKEN_PLUS: op = "+"; break;
                case TOKEN_MINUS: op = "-"; break;
                case TOKEN_STAR: op = "*"; break;
                case TOKEN_SLASH: op = "/"; break;
                case TOKEN_MOD: op = "%"; break;
                case TOKEN_EQ: op = "=="; break;
                case TOKEN_NEQ: op = "!="; break;
                case TOKEN_LT: op = "<"; break;
                case TOKEN_GT: op = ">"; break;
                case TOKEN_LTE: op = "<="; break;
                case TOKEN_GTE: op = ">="; break;
                case TOKEN_AND: op = "&"; break;
                case TOKEN_OR: op = "|"; break;
                case TOKEN_XOR: op = "^"; break;
                case TOKEN_LSHIFT: op = "<<"; break;
                case TOKEN_RSHIFT: op = ">>"; break;
                case TOKEN_AND_AND: op = "&&"; break;
                case TOKEN_OR_OR: op = "||"; break;
                default: op = "?"; break;
            }
            sb_append_fmt(sb, " %s ", op);
            parser_emit_ast_node(sb, bn->right, 0);
            sb_append(sb, ")");
            break;
        }

        case NODE_UNARY_OP: {
            UnaryOpNode *un = (UnaryOpNode*)node;
            const char *op = "";
            switch (un->op) {
                case TOKEN_MINUS: op = "-"; break;
                case TOKEN_NOT: op = "!"; break;
                case TOKEN_BIT_NOT: op = "~"; break;
                case TOKEN_STAR: op = "*"; break;
                case TOKEN_AND: op = "&"; break;
            }
            sb_append(sb, op);
            sb_append(sb, "(");
            parser_emit_ast_node(sb, un->operand, 0);
            sb_append(sb, ")");
            break;
        }

        case NODE_INC_DEC: {
            IncDecNode *id = (IncDecNode*)node;
            const char *op = (id->op == TOKEN_INCREMENT) ? "++" : "--";
            if (id->is_prefix) {
                sb_append(sb, op);
                parser_emit_ast_node(sb, id->target, 0);
            } else {
                parser_emit_ast_node(sb, id->target, 0);
                sb_append(sb, op);
            }
            break;
        }
        
        case NODE_LITERAL: {
            LiteralNode *ln = (LiteralNode*)node;
            if (ln->var_type.base == TYPE_INT) {
                if (ln->var_type.is_unsigned) sb_append_fmt(sb, "%u", (unsigned int)ln->val.long_val);
                else sb_append_fmt(sb, "%d", (int)ln->val.long_val);
            }
            else if (ln->var_type.base == TYPE_LONG) {
                if (ln->var_type.is_unsigned) sb_append_fmt(sb, "%luUL", (unsigned long)ln->val.long_val);
                else sb_append_fmt(sb, "%ldL", (long)ln->val.long_val);
            }
            else if (ln->var_type.base == TYPE_LONG_LONG) {
                 if (ln->var_type.is_unsigned) sb_append_fmt(sb, "%lluULL", (unsigned long long)ln->val.long_val);
                 else sb_append_fmt(sb, "%lldLL", (long long)ln->val.long_val);
            }
            else if (ln->var_type.base == TYPE_FLOAT) {
                 sb_append_fmt(sb, "%ff", ln->val.double_val);
            }
            else if (ln->var_type.base == TYPE_DOUBLE || ln->var_type.base == TYPE_LONG_DOUBLE) {
                 sb_append_fmt(sb, "%f", ln->val.double_val);
            }
            else if (ln->var_type.base == TYPE_STRING) {
                sb_append(sb, "\"");
                sb_append_escaped(sb, ln->val.str_val);
                sb_append(sb, "\"");
            }
            else if (ln->var_type.base == TYPE_CHAR) {
                if (ln->var_type.ptr_depth > 0) {
                    sb_append(sb, "c\"");
                    sb_append_escaped(sb, ln->val.str_val);
                    sb_append(sb, "\"");
                } else {
                    sb_append(sb, "'");
                    char c = (char)ln->val.long_val;
                    if (c == '\n') sb_append(sb, "\\n");
                    else if (c == '\t') sb_append(sb, "\\t");
                    else if (c == '\r') sb_append(sb, "\\r");
                    else if (c == '\\') sb_append(sb, "\\\\");
                    else if (c == '\'') sb_append(sb, "\\'");
                    else if (c == '\0') sb_append(sb, "\\0");
                    else {
                        char tmp[2] = {c, 0};
                        sb_append(sb, tmp);
                    }
                    sb_append(sb, "'");
                }
            }
            else if (ln->var_type.base == TYPE_BOOL) {
                sb_append(sb, ln->val.long_val ? "true" : "false");
            }
            else {
                 sb_append_fmt(sb, "%d", (int)ln->val.long_val);
            }
            break;
        }

        case NODE_ARRAY_LIT: {
            ArrayLitNode *an = (ArrayLitNode*)node;
            sb_append(sb, "[");
            ASTNode *el = an->elements;
            while(el) {
                parser_emit_ast_node(sb, el, 0);
                if (el->next) sb_append(sb, ", ");
                el = el->next;
            }
            sb_append(sb, "]");
            break;
        }

        case NODE_VAR_REF: {
            VarRefNode *vn = (VarRefNode*)node;
            sb_append(sb, vn->name);
            break;
        }

        case NODE_ARRAY_ACCESS: {
            ArrayAccessNode *aa = (ArrayAccessNode*)node;
            parser_emit_ast_node(sb, aa->target, 0);
            sb_append(sb, "[");
            parser_emit_ast_node(sb, aa->index, 0);
            sb_append(sb, "]");
            break;
        }
        
        case NODE_MEMBER_ACCESS: {
            MemberAccessNode *ma = (MemberAccessNode*)node;
            parser_emit_ast_node(sb, ma->object, 0);
            sb_append_fmt(sb, ".%s", ma->member_name);
            break;
        }

        case NODE_CAST: {
            CastNode *cn = (CastNode*)node;
            parser_emit_ast_node(sb, cn->operand, 0);
            sb_append(sb, " as ");
            parser_emit_type(sb, cn->var_type);
            break;
        }
        
        case NODE_LINK: {
            LinkNode *ln = (LinkNode*)node;
            sb_append_fmt(sb, "link \"%s\";", ln->lib_name);
            break;
        }
        
        case NODE_TYPEOF: {
            UnaryOpNode *un = (UnaryOpNode*)node;
            sb_append(sb, "typeof(");
            parser_emit_ast_node(sb, un->operand, 0);
            sb_append(sb, ")");
            break;
        }

        case NODE_TRAIT_ACCESS: {
            TraitAccessNode *ta = (TraitAccessNode*)node;
            parser_emit_ast_node(sb, ta->object, 0);
            sb_append_fmt(sb, "[%s]", ta->trait_name);
            break;
        }
        
        default:
            sb_append_fmt(sb, "/* Unhandled Node Type: %d */", node->type);
            break;
    }
}

char* parser_to_string(Parser *parser, ASTNode *root) {
    StringBuilder sb;
    sb_init(&sb, parser->ctx->arena);

    ASTNode *curr = root;
    while (curr) {
        parser_emit_ast_node(&sb, curr, 0);
        if (needs_semicolon(curr)) sb_append(&sb, ";");
        sb_append(&sb, "\n");
        curr = curr->next;
    }
    
    return sb.data;
}

void parser_to_file(Parser *parser, ASTNode *root, const char *filename) {
    char *str = parser_to_string(parser, root);
    if (str) {
        FILE *f = fopen(filename, "w");
        if (f) {
            fputs(str, f);
            fclose(f);
        }
    }
}

char* parser_string_to_string(const char *src) {
    Arena arena;
    arena_init(&arena);
    
    CompilerContext ctx;
    context_init(&ctx, &arena);
    
    Lexer l;
    lexer_init(&l, &ctx, "", src);
    
    Parser p;
    parser_init(&p, &l);
    
    ASTNode *root = parse_program(&p);
    char *res = parser_to_string(&p, root);
    
    arena_free(&arena);
    return res;
}

void parser_string_to_file(const char *src, const char *filename) {
    Arena arena;
    arena_init(&arena);
    
    CompilerContext ctx;
    context_init(&ctx, &arena);
    
    Lexer l;
    lexer_init(&l, &ctx, filename, src);
    
    Parser p;
    parser_init(&p, &l);
    
    ASTNode *root = parse_program(&p);
    parser_to_file(&p, root, filename);
    
    arena_free(&arena);
}
