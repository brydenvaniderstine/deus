#include "ir.h"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

/* Top-level declarations (typed-IR step 4, docs/M2_PLAN.md "Names, scopes,
 * items" and "Types"): the one top-level namespace (structs and functions),
 * duplicate names, struct fields, and type resolution. Diagnostics come out
 * grouped by check, each group in declaration order (E0210 for every
 * declaration, then E0211/E0220/E0227 struct by struct, ...). */

/* ---- open-addressing name table: looked up, never iterated ---- */

typedef struct {
    const char *name;     /* NULL = empty slot */
    Span        name_span;
    bool        is_struct;
    size_t      index;    /* structs: position in declaration order */
} SymEntry;

typedef struct {
    SymEntry *slots; /* arena-allocated, cap entries, name == NULL = empty */
    size_t    cap;   /* power of two */
} Symtab;

static uint64_t name_hash(const char *s) {
    uint64_t h = 1469598103934665603ULL; /* FNV-1a */
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

/* Capacity for `n` entries at load <= 1/2. */
static void symtab_init(IrState *st, Symtab *t, size_t n) {
    size_t cap = 8;
    while (cap < n * 2) cap *= 2;
    t->slots = arena_alloc(st->arena, cap * sizeof(SymEntry), alignof(SymEntry));
    memset(t->slots, 0, cap * sizeof(SymEntry));
    t->cap = cap;
}

static const SymEntry *symtab_lookup(const Symtab *t, const char *name) {
    size_t mask = t->cap - 1;
    for (size_t i = (size_t)(name_hash(name) & mask);; i = (i + 1) & mask) {
        if (!t->slots[i].name) return NULL;
        if (strcmp(t->slots[i].name, name) == 0) return &t->slots[i];
    }
}

/* Inserts unless the name is present; returns the entry that holds the name
 * (the new one, or the first declaration's). The table is sized for its keys
 * up front, so it can never fill. */
static const SymEntry *symtab_insert(Symtab *t, const char *name, Span name_span, bool is_struct, size_t index, bool *inserted) {
    size_t mask = t->cap - 1;
    for (size_t i = (size_t)(name_hash(name) & mask);; i = (i + 1) & mask) {
        if (!t->slots[i].name) {
            t->slots[i].name = name;
            t->slots[i].name_span = name_span;
            t->slots[i].is_struct = is_struct;
            t->slots[i].index = index;
            *inserted = true;
            return &t->slots[i];
        }
        if (strcmp(t->slots[i].name, name) == 0) {
            *inserted = false;
            return &t->slots[i];
        }
    }
}

/* ---- declarations ---- */

typedef struct {
    const Decl *decl;
    IrStruct   *ir;
    bool        poisoned; /* a field type failed to resolve: no layout, no further errors from it */
} StructInfo;

typedef struct {
    IrState    *st;
    Symtab      top;     /* structs and functions, one namespace */
    StructInfo *structs; /* declaration order */
    size_t      n_structs;
    IrStruct   *irs;     /* contiguous: struct i is irs[i] */
} Ctx;

static const char *decl_name(const Decl *d) {
    return d->kind == DECL_STRUCT ? d->as.struct_decl.name : d->as.function_decl.name;
}

static Span decl_name_span(const Decl *d) {
    return d->kind == DECL_STRUCT ? d->as.struct_decl.name_span : d->as.function_decl.name_span;
}

static const IrType *prim_type(IrState *st, const char *name) {
    static const struct { const char *name; int bits; bool sg; } ints[] = {
        {"u8", 8, false}, {"i8", 8, true}, {"u16", 16, false}, {"i16", 16, true},
        {"u32", 32, false}, {"i32", 32, true}, {"u64", 64, false}, {"i64", 64, true},
    };
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++) {
        if (strcmp(name, ints[i].name) == 0) return ir_type_int(st, ints[i].bits, ints[i].sg);
    }
    return strcmp(name, "bool") == 0 ? ir_type_bool(st) : NULL;
}

/* AST type -> IR type. A failure records ONE error and yields ERROR, which
 * every enclosing array silently propagates (one mistake, one error). Recursion
 * is once per nested array/slice level: the parser bounds that to 254. */
static const IrType *resolve_type(Ctx *cx, const Type *t) {
    IrState *st = cx->st;
    switch (t->kind) {
        case TYPE_IDENT: {
            const IrType *p = prim_type(st, t->as.ident.name);
            if (p) return p;
            const SymEntry *e = symtab_lookup(&cx->top, t->as.ident.name);
            if (e && e->is_struct) return ir_type_struct(st, &cx->irs[e->index]);
            ir_error(st, diag_ir_unknown_type(st->arena, t->span, t->as.ident.name));
            return ir_type_error(st);
        }
        case TYPE_ARRAY: {
            const IrType *elem = resolve_type(cx, t->as.array.elem);
            if (elem->kind == IR_T_ERROR) return elem;
            return ir_type_array(st, t->as.array.size, elem);
        }
        case TYPE_SLICE:
            break;
    }
    /* A slice. Step 4 resolves types only for struct fields, where a slice is never
     * allowed (docs/M2_PLAN.md "Arrays, slices, storage"); parameters and locals,
     * where it is, arrive in later steps with their own context. */
    ir_error(st, diag_ir_slice_in_field(t->span));
    return ir_type_error(st);
}

/* Pass 1: every top-level name into the one namespace. The first declaration
 * of a name wins; each later one is E0210 at its name, naming the first. */
static void declare_names(Ctx *cx, const NodeList *items) {
    IrState *st = cx->st;
    size_t si = 0;
    for (size_t i = 0; i < items->len; i++) {
        const Decl *d = items->items[i];
        st->progress = d->span;
        bool is_struct = d->kind == DECL_STRUCT;
        if (is_struct) {
            cx->structs[si].decl = d;
            cx->structs[si].ir = &cx->irs[si];
        }
        bool inserted;
        const SymEntry *e = symtab_insert(&cx->top, decl_name(d), decl_name_span(d), is_struct, si, &inserted);
        if (!inserted) ir_error(st, diag_ir_duplicate_decl(st->arena, decl_name_span(d), decl_name(d), e->name_span));
        if (is_struct) si++;
    }
}

/* Pass 2: each struct's fields, in order: a repeated field name is E0211 (the
 * first wins; the repeat's type is still resolved for its own errors, but it is
 * not a field), and each field type is resolved. */
static void build_fields(Ctx *cx, StructInfo *s) {
    IrState *st = cx->st;
    const Decl *d = s->decl;
    const NodeList *fl = &d->as.struct_decl.fields;
    st->progress = d->span;

    IrStruct *ir = s->ir;
    memset(ir, 0, sizeof *ir);
    ir->name = d->as.struct_decl.name;
    ir->span = d->span;
    ir->align = 1;
    ir->fields = arena_alloc(st->arena, (fl->len ? fl->len : 1) * sizeof(IrField), alignof(IrField));

    Symtab seen;
    symtab_init(st, &seen, fl->len);
    for (size_t i = 0; i < fl->len; i++) {
        const Field *f = fl->items[i];
        bool inserted;
        symtab_insert(&seen, f->name, f->name_span, false, 0, &inserted);
        if (!inserted) ir_error(st, diag_ir_duplicate_field(st->arena, f->name_span, f->name, ir->name));
        const IrType *t = resolve_type(cx, f->type);
        if (t->kind == IR_T_ERROR) s->poisoned = true;
        if (!inserted) continue;
        IrField *out = &ir->fields[ir->n_fields++];
        out->name = f->name;
        out->span = f->span;
        out->type = t;
        out->offset = 0;
    }
}

bool ir_declare_items(IrState *st, const NodeList *items, IrModule *m) {
    (void)m;
    int errors_before = st->error_count;
    Ctx cx = {0};
    cx.st = st;
    size_t n = 0;
    for (size_t i = 0; i < items->len; i++) n += ((const Decl *)items->items[i])->kind == DECL_STRUCT;
    cx.n_structs = n;
    symtab_init(st, &cx.top, items->len);
    cx.structs = arena_alloc(st->arena, (n ? n : 1) * sizeof(StructInfo), alignof(StructInfo));
    cx.irs = arena_alloc(st->arena, (n ? n : 1) * sizeof(IrStruct), alignof(IrStruct));
    memset(cx.structs, 0, (n ? n : 1) * sizeof(StructInfo));

    declare_names(&cx, items);
    for (size_t i = 0; i < n; i++) build_fields(&cx, &cx.structs[i]);
    return st->error_count == errors_before;
}
