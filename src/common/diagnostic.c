#include "diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void diag_set_namespace(CompilerContext *ctx, const char *ns) {
    if (!ctx) return;
    if (ns && strlen(ns) > 0) {
        strncpy(ctx->current_namespace, ns, 255);
        ctx->current_namespace[255] = '\0';
    } else {
        strcpy(ctx->current_namespace, "main");
    }
}

const char* diag_get_namespace(CompilerContext *ctx) {
    if (!ctx) return "unknown";
    return ctx->current_namespace;
}

// Helper to shorten path to last 3 components
static void get_short_path(const char *in, char *out, size_t size) {
    if (!in || strlen(in) == 0) { 
        out[0] = 0; 
        return; 
    }
    
    int slashes = 0;
    const char *p = in;
    while(*p) { if (*p == '/' || *p == '\\') slashes++; p++; }
    
    if (slashes <= 2) {
        strncpy(out, in, size);
        out[size-1] = '\0';
        return;
    }
    
    int count = 0;
    p = in + strlen(in) - 1;
    while(p > in) {
        if (*p == '/' || *p == '\\') {
            count++;
            if (count == 3) {
                p++; 
                break;
            }
        }
        p--;
    }
    
    snprintf(out, size, ".../%s", p);
}

// --- Levenshtein Distance (Stateless) ---
int min3(int a, int b, int c) {
    int m = a;
    if (b < m) m = b;
    if (c < m) m = c;
    return m;
}

int levenshtein_dist(const char *s1, const char *s2) {
    if (!s1 || !s2) return 100;
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    // Note: VLA usage for matrix, acceptable for small keywords
    int matrix[len1 + 1][len2 + 1];

    for (int i = 0; i <= len1; i++) matrix[i][0] = i;
    for (int j = 0; j <= len2; j++) matrix[0][j] = j;

    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            int res = min3(
                matrix[i - 1][j] + 1,      
                matrix[i][j - 1] + 1,      
                matrix[i - 1][j - 1] + cost 
            );

            if (i > 1 && j > 1 && 
                s1[i - 1] == s2[j - 2] && 
                s1[i - 2] == s2[j - 1]) {
                int trans = matrix[i - 2][j - 2] + 1; 
                if (trans < res) res = trans;
            }
            matrix[i][j] = res;
        }
    }
    return matrix[len1][len2];
}

const char* find_closest_keyword(const char *ident) {
    const char *keywords[] = {
        "int", "void", "char", "bool", "single", "double", "return", 
        "if", "else", "while", "loop", "break", "continue", "class", "struct",
        "namespace", "import", "link", "extern", "define", "has", "is",
        "open", "closed", "let", "mut", "imut", "typeof", 
        "switch", "case", "default", "leak", 
        NULL
    };
    
    const char *best = NULL;
    int min_dist = 100;
    
    for (int i = 0; keywords[i] != NULL; i++) {
        int d = levenshtein_dist(ident, keywords[i]);
        if (d < min_dist && d < 3) { 
            min_dist = d;
            best = keywords[i];
        }
    }
    return best;
}

static void print_source_snippet(Lexer *l, Token t) {
    if (!l || !l->src) return;

    const char *line_start = l->src;
    int current_line = 1;
    
    while (*line_start && current_line < t.line) {
        if (*line_start == '\n') current_line++;
        line_start++;
    }
    
    const char *line_end = line_start;
    while (*line_end && *line_end != '\n') line_end++;
    
    int line_len = (int)(line_end - line_start);
    
    fprintf(stderr, "  %s|%s %.*s\n", DIAG_GREY, DIAG_RESET, line_len, line_start);
    fprintf(stderr, "  %s|%s ", DIAG_GREY, DIAG_RESET);
    for (int i = 1; i < t.col; i++) fprintf(stderr, " ");
    fprintf(stderr, "%s^%s\n", DIAG_BOLD, DIAG_RESET);
}

static void report_generic(Lexer *l, Token t, const char *label, const char *color, const char *msg) {
    if (!l || !l->ctx) return;
    CompilerContext *ctx = l->ctx;

    // Check if Namespace context changed
    if (strcmp(ctx->current_namespace, ctx->last_reported_namespace) != 0) {
        fprintf(stderr, "at namespace %s%s%s:\n", DIAG_BOLD, ctx->current_namespace, DIAG_RESET);
        strncpy(ctx->last_reported_namespace, ctx->current_namespace, 255);
        ctx->last_reported_namespace[255] = '\0';
    }
    
    // Check if File context changed
    if (l->filename) {
        if (strcmp(l->filename, ctx->last_reported_filename) != 0) {
            char short_path[256];
            get_short_path(l->filename, short_path, sizeof(short_path));
            fprintf(stderr, "in %s%s%s:\n", DIAG_PURPLE, short_path, DIAG_RESET);
            
            strncpy(ctx->last_reported_filename, l->filename, 1023);
            ctx->last_reported_filename[1023] = '\0';
        }
    }
    
    fprintf(stderr, "%d:%d: %s%s%s: %s\n", 
            t.line, t.col, 
            color, label, DIAG_RESET, 
            msg);
            
    print_source_snippet(l, t);
}

void report_error(Lexer *l, Token t, const char *msg) {
    report_generic(l, t, "error", DIAG_RED, msg);
    if (l && l->ctx) l->ctx->error_count++;
}

void report_warning(Lexer *l, Token t, const char *msg) {
    report_generic(l, t, "warning", DIAG_PURPLE, msg);
}

void report_hint(Lexer *l, Token t, const char *msg) {
    (void)l; (void)t; 
    fprintf(stderr, "%shint:%s %s\n", DIAG_YELLOW, DIAG_RESET, msg);
}

void report_info(Lexer *l, Token t, const char *msg) {
    report_generic(l, t, "info", DIAG_BLUE, msg);
}

void report_reason(Lexer *l, Token t, const char *msg) {
    fprintf(stderr, "%d:%d: %sreason:%s %s\n", t.line, t.col, DIAG_PURPLE, DIAG_RESET, msg);
    if (l) print_source_snippet(l, t);
}

// Token to String mappings (Stateless)
const char* token_type_to_string(TokenType type) {
    switch (type) {
        case TOKEN_EOF: return "EOF";
        case TOKEN_LOOP: return "loop";
        case TOKEN_WHILE: return "while";
        case TOKEN_ONCE: return "once";
        case TOKEN_LBRACKET: return "[";
        case TOKEN_RBRACKET: return "]";
        case TOKEN_LBRACE: return "{";
        case TOKEN_RBRACE: return "}";
        case TOKEN_LPAREN: return "(";
        case TOKEN_RPAREN: return ")";
        case TOKEN_SEMICOLON: return ";";
        case TOKEN_COLON: return ":";
        case TOKEN_COMMA: return ",";
        case TOKEN_ELLIPSIS: return "...";
        case TOKEN_DOT: return ".";

        case TOKEN_NUMBER: return "number";
        case TOKEN_FLOAT: return "float";
        case TOKEN_STRING: return "string";
        case TOKEN_C_STRING: return "c-string";
        case TOKEN_CHAR_LIT: return "char";
        case TOKEN_IDENTIFIER: return "identifier";

        case TOKEN_ASSIGN: return "=";
        case TOKEN_PLUS_ASSIGN: return "+=";
        case TOKEN_MINUS_ASSIGN: return "-=";
        case TOKEN_STAR_ASSIGN: return "*=";
        case TOKEN_SLASH_ASSIGN: return "/=";
        case TOKEN_MOD_ASSIGN: return "%=";
        case TOKEN_AND_ASSIGN: return "&=";
        case TOKEN_OR_ASSIGN: return "|=";
        case TOKEN_XOR_ASSIGN: return "^=";
        case TOKEN_LSHIFT_ASSIGN: return "<<=";
        case TOKEN_RSHIFT_ASSIGN: return ">>=";

        case TOKEN_IF: return "if";
        case TOKEN_ELIF: return "elif";
        case TOKEN_ELSE: return "else";
        case TOKEN_RETURN: return "return";
        case TOKEN_BREAK: return "break";
        case TOKEN_CONTINUE: return "continue";
        case TOKEN_SWITCH: return "switch";
        case TOKEN_CASE: return "case";
        case TOKEN_DEFAULT: return "default";
        case TOKEN_LEAK: return "leak";

        case TOKEN_DEFINE: return "define";
        case TOKEN_AS: return "as";
        case TOKEN_TYPEDEF: return "typedef";

        case TOKEN_CLASS: return "class";
        case TOKEN_STRUCT: return "struct";
        case TOKEN_UNION: return "union";
        case TOKEN_IS: return "is";
        case TOKEN_HAS: return "has";
        case TOKEN_OPEN: return "open";
        case TOKEN_CLOSED: return "closed";
        case TOKEN_TYPEOF: return "typeof";
        case TOKEN_HASMETHOD: return "hasmethod";
        case TOKEN_HASATTRIBUTE: return "hasattribute";

        case TOKEN_NAMESPACE: return "namespace";
        case TOKEN_ENUM: return "enum";

        case TOKEN_FLUX: return "flux";
        case TOKEN_EMIT: return "emit";
        case TOKEN_FOR: return "for";
        case TOKEN_IN: return "in";

        case TOKEN_KW_VOID: return "void";
        case TOKEN_KW_INT: return "int";
        case TOKEN_KW_CHAR: return "char";
        case TOKEN_KW_BOOL: return "bool";
        case TOKEN_KW_SINGLE: return "single";
        case TOKEN_KW_DOUBLE: return "double";
        case TOKEN_KW_STRING: return "string";
        case TOKEN_KW_LET: return "let";

        case TOKEN_KW_SHORT: return "short";
        case TOKEN_KW_LONG: return "long";
        case TOKEN_KW_UNSIGNED: return "unsigned";

        case TOKEN_ULONG_LONG_LIT: return "ulong long";
        case TOKEN_LONG_LONG_LIT: return "long long";
        case TOKEN_ULONG_LIT: return "ulong";
        case TOKEN_LONG_LIT: return "long";
        case TOKEN_UINT_LIT: return "uint";
        case TOKEN_LONG_DOUBLE_LIT: return "long double";

        case TOKEN_KW_MUT: return "mut";
        case TOKEN_KW_IMUT: return "imut";

        case TOKEN_IMPORT: return "import";
        case TOKEN_EXTERN: return "extern";
        case TOKEN_LINK: return "link";

        case TOKEN_TRUE: return "true";
        case TOKEN_FALSE: return "false";

        case TOKEN_NOT: return "!";
        case TOKEN_BIT_NOT: return "~";

        case TOKEN_PLUS: return "+";
        case TOKEN_INCREMENT: return "++";
        case TOKEN_MINUS: return "-";
        case TOKEN_DECREMENT: return "--";
        case TOKEN_STAR: return "*";
        case TOKEN_SLASH: return "/";
        case TOKEN_MOD: return "%";
        case TOKEN_AND: return "&";
        case TOKEN_OR: return "|";
        case TOKEN_XOR: return "^";
        case TOKEN_LSHIFT: return "<<";
        case TOKEN_RSHIFT: return ">>";

        case TOKEN_AND_AND: return "&&";
        case TOKEN_OR_OR: return "||";

        case TOKEN_EQ: return "==";
        case TOKEN_NEQ: return "!=";
        case TOKEN_LT: return "<";
        case TOKEN_GT: return ">";
        case TOKEN_LTE: return "<=";
        case TOKEN_GTE: return ">=";

        case TOKEN_UNKNOWN: return "unknown";
        default: return "unknown_token";
    }
}

const char* get_token_description(TokenType type) {
    switch(type) {
        case TOKEN_SEMICOLON: return "';'";
        case TOKEN_RBRACE: return "'}'";
        case TOKEN_RPAREN: return "')'";
        default: return "token";
    }
}
