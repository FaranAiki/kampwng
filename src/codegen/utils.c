#include "codegen.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

char* format_string(const char* input) {
  if (!input) return NULL;
  size_t len = strlen(input);
  char *new_str = malloc(len + 1);
  strcpy(new_str, input);
  return new_str;
}

void get_type_name(VarType t, char *buf) {
    char base_buf[128] = "";
    if (t.is_unsigned) strcpy(base_buf, "unsigned ");

    if (t.is_func_ptr) {
        // Recursively get return type name
        char ret_buf[64];
        get_type_name(*t.fp_ret_type, ret_buf);
        sprintf(base_buf, "%s (*)(", ret_buf);
        
        for (int i=0; i<t.fp_param_count; i++) {
            char p_buf[64];
            get_type_name(t.fp_param_types[i], p_buf);
            strcat(base_buf, p_buf);
            if (i < t.fp_param_count - 1) strcat(base_buf, ", ");
        }
        if (t.fp_is_varargs) {
            if (t.fp_param_count > 0) strcat(base_buf, ", ");
            strcat(base_buf, "...");
        }
        strcat(base_buf, ")");
        strcpy(buf, base_buf);
        return;
    }

    switch (t.base) {
        case TYPE_INT: strcat(base_buf, "int"); break;
        case TYPE_SHORT: strcat(base_buf, "short"); break;
        case TYPE_LONG: strcat(base_buf, "long"); break;
        case TYPE_LONG_LONG: strcat(base_buf, "long long"); break;
        case TYPE_CHAR: strcat(base_buf, "char"); break;
        case TYPE_BOOL: strcat(base_buf, "bool"); break;
        case TYPE_FLOAT: strcat(base_buf, "float"); break;
        case TYPE_DOUBLE: strcat(base_buf, "double"); break;
        case TYPE_LONG_DOUBLE: strcat(base_buf, "long double"); break;
        case TYPE_VOID: strcat(base_buf, "void"); break;
        case TYPE_STRING: strcat(base_buf, "string"); break;
        case TYPE_CLASS: 
            strcat(base_buf, t.class_name ? t.class_name : "object"); 
            break;
        default: strcat(base_buf, "unknown"); break;
    }
    
    strcpy(buf, base_buf);
    
    // Pointers
    for(int i=0; i<t.ptr_depth; i++) strcat(buf, "*");
    
    // Arrays
    if (t.array_size > 0) {
        char tmp[32];
        sprintf(tmp, "[%d]", t.array_size);
        strcat(buf, tmp);
    }
}
