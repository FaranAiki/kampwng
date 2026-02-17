#include "alir.h"

AlirValue* alir_const_int(long val) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_CONST;
    v->type = (VarType){TYPE_INT, 0, NULL, 0, 0};
    v->int_val = val;
    return v;
}

AlirValue* alir_const_float(double val) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_CONST;
    v->type = (VarType){TYPE_DOUBLE, 0, NULL, 0, 0};
    v->float_val = val;
    return v;
}

AlirValue* alir_val_temp(VarType t, int id) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_TEMP;
    v->type = t;
    v->temp_id = id;
    return v;
}

AlirValue* alir_val_var(const char *name) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_VAR;
    v->str_val = strdup(name);
    return v;
}

AlirValue* alir_val_global(const char *name, VarType type) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_GLOBAL;
    v->str_val = strdup(name);
    v->type = type;
    return v;
}

AlirValue* alir_val_label(const char *label) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_LABEL;
    v->str_val = strdup(label);
    return v;
}

AlirValue* alir_val_type(const char *type_name) {
    AlirValue *v = calloc(1, sizeof(AlirValue));
    v->kind = ALIR_VAL_TYPE;
    v->str_val = strdup(type_name);
    v->type = (VarType){TYPE_CLASS, 0, strdup(type_name), 0, 0};
    return v;
}

