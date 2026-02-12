#include "diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global Context Tracker
static char current_namespace[256] = "main";
static char last_reported_namespace[256] = ""; 
static char last_reported_filename[1024] = "";

void diag_set_namespace(const char *ns) {
    if (ns && strlen(ns) > 0) {
        strncpy(current_namespace, ns, 255);
        current_namespace[255] = '\0';
    } else {
        strcpy(current_namespace, "main");
    }
}

const char* diag_get_namespace(void) {
    return current_namespace;
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
    
    // If path is short enough, return as is
    if (slashes <= 2) {
        strncpy(out, in, size);
        out[size-1] = '\0';
        return;
    }
    
    // Find the 3rd slash from the end
    int count = 0;
    p = in + strlen(in) - 1;
    while(p > in) {
        if (*p == '/' || *p == '\\') {
            count++;
            if (count == 3) {
                p++; // Move past the slash to start of the component
                break;
            }
        }
        p--;
    }
    
    snprintf(out, size, ".../%s", p);
}

// --- Levenshtein Distance ---
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
    int matrix[len1 + 1][len2 + 1];

    for (int i = 0; i <= len1; i++) matrix[i][0] = i;
    for (int j = 0; j <= len2; j++) matrix[0][j] = j;

    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            
            // Standard Levenshtein operations
            int res = min3(
                matrix[i - 1][j] + 1,      // Deletion
                matrix[i][j - 1] + 1,      // Insertion
                matrix[i - 1][j - 1] + cost // Substitution
            );

            // Damerau-Levenshtein Transposition check
            // Checks if swapping adjacent characters creates a match
            if (i > 1 && j > 1 && 
                s1[i - 1] == s2[j - 2] && 
                s1[i - 2] == s2[j - 1]) {
                
                int trans = matrix[i - 2][j - 2] + 1; // Cost of transposition is 1
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
        if (d < min_dist && d < 3) { // Threshold for typos
            min_dist = d;
            best = keywords[i];
        }
    }
    return best;
}

static void print_source_snippet(Lexer *l, Token t) {
    if (!l || !l->src) return;

    // Find the start of the line
    const char *line_start = l->src;
    int current_line = 1;
    
    // Navigate to the correct line
    while (*line_start && current_line < t.line) {
        if (*line_start == '\n') current_line++;
        line_start++;
    }
    
    // Find end of line
    const char *line_end = line_start;
    while (*line_end && *line_end != '\n') line_end++;
    
    int line_len = (int)(line_end - line_start);
    
    fprintf(stderr, "  %s|%s %.*s\n", DIAG_GREY, DIAG_RESET, line_len, line_start);
    fprintf(stderr, "  %s|%s ", DIAG_GREY, DIAG_RESET);
    // Be careful with tabs, but assuming spaces for now or simple offset
    for (int i = 1; i < t.col; i++) fprintf(stderr, " ");
    fprintf(stderr, "%s^%s\n", DIAG_BOLD, DIAG_RESET);
}

static void report_generic(Lexer *l, Token t, const char *label, const char *color, const char *msg) {
    // Namespace info: Only print if context has changed
    if (strcmp(current_namespace, last_reported_namespace) != 0) {
        fprintf(stderr, "at namespace %s%s%s:\n", DIAG_BOLD, current_namespace, DIAG_RESET);
        strncpy(last_reported_namespace, current_namespace, 255);
        last_reported_namespace[255] = '\0';
    }
    
    // File Context info
    if (l && l->filename) {
        if (strcmp(l->filename, last_reported_filename) != 0) {
            char short_path[256];
            get_short_path(l->filename, short_path, sizeof(short_path));
            fprintf(stderr, "in %s%s%s:\n", DIAG_PURPLE, short_path, DIAG_RESET);
            
            strncpy(last_reported_filename, l->filename, 1023);
            last_reported_filename[1023] = '\0';
        }
    }
    
    // Diagnostic Line
    fprintf(stderr, "%d:%d: %s%s%s: %s\n", 
            t.line, t.col, 
            color, label, DIAG_RESET, 
            msg);
            
    if (l) print_source_snippet(l, t);
}

void report_error(Lexer *l, Token t, const char *msg) {
    report_generic(l, t, "error", DIAG_RED, msg);
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

const char* token_type_to_string(TokenType type) {
    switch (type) {
        case TOKEN_IDENTIFIER: return "identifier";
        case TOKEN_LPAREN: return "(";
        case TOKEN_RPAREN: return ")";
        case TOKEN_LBRACE: return "{";
        case TOKEN_RBRACE: return "}";
        case TOKEN_LBRACKET: return "[";
        case TOKEN_RBRACKET: return "]";
        case TOKEN_SEMICOLON: return ";";
        case TOKEN_COMMA: return ",";
        case TOKEN_ASSIGN: return "=";
        case TOKEN_EOF: return "EOF";
        default: return "token";
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
