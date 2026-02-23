#include "alir.h"

AlirValue* alir_const_int(AlirModule *mod, long val) {
    AlirValue *v = alir_alloc(mod, sizeof(AlirValue));
    v->kind = ALIR_VAL_CONST;
    v->type = (VarType){TYPE_INT, 0};
    v->int_val = val;
    return v;
}

AlirValue* alir_const_float(AlirModule *mod, double val) {
    AlirValue *v = alir_alloc(mod, sizeof(AlirValue));
    v->kind = ALIR_VAL_CONST;
    v->type = (VarType){TYPE_DOUBLE, 0};
    v->float_val = val;
    return v;
}

AlirValue* alir_val_temp(AlirModule *mod, VarType t, int id) {
    AlirValue *v = alir_alloc(mod, sizeof(AlirValue));
    v->kind = ALIR_VAL_TEMP;
    v->type = t;
    v->temp_id = id;
    return v;
}

AlirValue* alir_val_var(AlirModule *mod, const char *name) {
    AlirValue *v = alir_alloc(mod, sizeof(AlirValue));
    v->kind = ALIR_VAL_VAR;
    v->str_val = alir_strdup(mod, name);
    return v;
}

AlirValue* alir_val_global(AlirModule *mod, const char *name, VarType type) {
    AlirValue *v = alir_alloc(mod, sizeof(AlirValue));
    v->kind = ALIR_VAL_GLOBAL;
    v->str_val = alir_strdup(mod, name);
    v->type = type;
    return v;
}

AlirValue* alir_val_label(AlirModule *mod, const char *label) {
    AlirValue *v = alir_alloc(mod, sizeof(AlirValue));
    v->kind = ALIR_VAL_LABEL;
    v->str_val = alir_strdup(mod, label);
    return v;
}

AlirValue* alir_val_type(AlirModule *mod, const char *type_name) {
    AlirValue *v = alir_alloc(mod, sizeof(AlirValue));
    v->kind = ALIR_VAL_TYPE;
    v->str_val = alir_strdup(mod, type_name);
    v->type = (VarType){TYPE_CLASS, 0, 0, alir_strdup(mod, type_name), 0, 0};
    return v;
}

void alir_register_enum(AlirModule *mod, const char *name, AlirEnumEntry *entries) {
    AlirEnum *e = alir_alloc(mod, sizeof(AlirEnum));
    e->name = alir_strdup(mod, name);
    e->entries = entries;
    e->next = mod->enums;
    mod->enums = e;
}

AlirEnum* alir_find_enum(AlirModule *mod, const char *name) {
    AlirEnum *curr = mod->enums;
    while(curr) {
        if (strcmp(curr->name, name) == 0) return curr;
        curr = curr->next;
    }
    return NULL;
}

int alir_get_enum_value(AlirModule *mod, const char *enum_name, const char *entry_name, long *out_val) {
    AlirEnum *e = alir_find_enum(mod, enum_name);
    if (!e) return 0;
    
    AlirEnumEntry *ent = e->entries;
    while(ent) {
        if (strcmp(ent->name, entry_name) == 0) {
            *out_val = ent->value;
            return 1;
        }
        ent = ent->next;
    }
    return 0;
}
