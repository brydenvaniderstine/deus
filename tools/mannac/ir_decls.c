#include "ir.h"
#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
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

/* Largest size of any single type (docs/M2_PLAN.md "Layout and limits": Wasm MVP
 * memory32, signed-offset friendly). */
#define IR_MAX_TYPE_SIZE ((uint64_t)INT32_MAX)

typedef struct {
    const Decl   *decl;
    IrStruct     *ir;
    const Type  **field_types; /* the AST type of each KEPT field (parallel to ir->fields) */
    bool          poisoned;    /* a field type failed to resolve: no layout, no further errors from it */
    bool          sized;       /* ir->size/align/offsets are valid */
    bool          cyclic;      /* member of a containment cycle (E0221 reported for the cycle) */
    bool          cycle_error; /* this member is where E0221 is reported (lowest declaration index) */
    bool          size_error;  /* E0222 to report at size_error_span */
    Span          size_error_span;
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
    s->field_types = arena_alloc(st->arena, (fl->len ? fl->len : 1) * sizeof(const Type *), alignof(const Type *));

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
        s->field_types[ir->n_fields] = f->type;
        IrField *out = &ir->fields[ir->n_fields++];
        out->name = f->name;
        out->span = f->span;
        out->type = t;
        out->offset = 0;
    }
}

/* ---- containment: which structs a struct holds BY VALUE ---- */

/* The struct a type holds by value, if any: arrays are looked through (a
 * [3]S holds S three times over), nothing else holds a struct. */
static const IrStruct *held_struct(const IrType *t) {
    while (t->kind == IR_T_ARRAY) t = t->as.array.elem;
    return t->kind == IR_T_STRUCT ? t->as.strukt.decl : NULL;
}

/* ---- layout ---- */

typedef enum { LAY_OK, LAY_TOO_BIG, LAY_UNSIZED } LayResult;

/* Size and alignment of a field type. `ast` mirrors `t` (an array nests the same
 * way in both), so a size error is located at the AST node that overflowed.
 * LAY_UNSIZED: it needs a struct with no layout (cyclic, or itself in error); that
 * is silent, the mistake is already reported. Recursion is once per nested array
 * level (the parser bounds that to 254). */
static LayResult type_layout(Ctx *cx, const Type *ast, const IrType *t, uint64_t *size, uint32_t *align, Span *bad) {
    switch (t->kind) {
        case IR_T_INT:
            *size = t->as.integer.bits / 8;
            *align = (uint32_t)*size;
            return LAY_OK;
        case IR_T_BOOL:
            *size = 1;
            *align = 1;
            return LAY_OK;
        case IR_T_ARRAY: {
            uint64_t es;
            uint32_t ea;
            LayResult r = type_layout(cx, ast->as.array.elem, t->as.array.elem, &es, &ea, bad);
            if (r != LAY_OK) return r;
            *align = ea;
            if (es == 0 || t->as.array.len == 0) {
                *size = 0;
            } else if (t->as.array.len > IR_MAX_TYPE_SIZE / es) { /* len * es would exceed the limit */
                *bad = ast->span;
                return LAY_TOO_BIG;
            } else {
                *size = t->as.array.len * es;
            }
            return LAY_OK;
        }
        default:
            break;
    }
    /* A struct: slices, unit and ERROR never reach here (a slice field is E0227, which
     * poisons the struct; unit is unwritable). */
    assert(t->kind == IR_T_STRUCT);
    const StructInfo *dep = &cx->structs[t->as.strukt.decl - cx->irs];
    if (!dep->sized) return LAY_UNSIZED;
    *size = dep->ir->size;
    *align = dep->ir->align;
    return LAY_OK;
}

static uint64_t align_up(uint64_t v, uint64_t a) {
    return (v + a - 1) / a * a;
}

/* Fields in declaration order, natural alignment per field, trailing padding to the
 * struct's alignment, no reordering (docs/M2_PLAN.md "Layout and limits"). All
 * intermediate values stay below 2^32 because each is checked against the limit
 * before the next addition, so nothing wraps. */
static void layout_struct(Ctx *cx, StructInfo *s) {
    if (s->poisoned) return;
    IrStruct *ir = s->ir;
    uint64_t off = 0;
    uint32_t max_align = 1;
    for (size_t i = 0; i < ir->n_fields; i++) {
        uint64_t fs;
        uint32_t fa;
        Span bad;
        LayResult r = type_layout(cx, s->field_types[i], ir->fields[i].type, &fs, &fa, &bad);
        if (r == LAY_UNSIZED) return;
        if (r == LAY_TOO_BIG) {
            s->size_error = true;
            s->size_error_span = bad;
            return;
        }
        off = align_up(off, fa);
        ir->fields[i].offset = off;
        off += fs;
        if (fa > max_align) max_align = fa;
        if (off > IR_MAX_TYPE_SIZE) {
            s->size_error = true;
            s->size_error_span = s->decl->as.struct_decl.name_span;
            return;
        }
    }
    uint64_t total = align_up(off, max_align);
    if (total > IR_MAX_TYPE_SIZE) {
        s->size_error = true;
        s->size_error_span = s->decl->as.struct_decl.name_span;
        return;
    }
    ir->size = total;
    ir->align = max_align;
    s->sized = true;
}

/* ---- cycles and layout order ---- */

/* Tarjan's strongly connected components over "holds by value", ITERATIVE: a chain
 * of structs each holding the next is as long as the input allows (tens of
 * thousands), far past any safe host-stack recursion. Components come out in
 * reverse topological order - everything a struct holds is complete before the
 * struct - so layout happens as each acyclic component is emitted. A component with
 * two or more members, or one holding itself, is a cycle: E0221 once per cycle, at
 * its lowest-declaration-index member, and none of its members gets a layout. */
static void analyze_structs(Ctx *cx) {
    IrState *st = cx->st;
    size_t n = cx->n_structs;
    if (n == 0) return;

    size_t *adj_start = arena_alloc(st->arena, (n + 1) * sizeof(size_t), alignof(size_t));
    size_t n_edges = 0;
    for (size_t i = 0; i < n; i++) {
        adj_start[i] = n_edges;
        const IrStruct *ir = cx->structs[i].ir;
        for (size_t f = 0; f < ir->n_fields; f++) n_edges += held_struct(ir->fields[f].type) != NULL;
    }
    adj_start[n] = n_edges;
    size_t *adj = arena_alloc(st->arena, (n_edges ? n_edges : 1) * sizeof(size_t), alignof(size_t));
    for (size_t i = 0, e = 0; i < n; i++) {
        const IrStruct *ir = cx->structs[i].ir;
        for (size_t f = 0; f < ir->n_fields; f++) {
            const IrStruct *h = held_struct(ir->fields[f].type);
            if (h) adj[e++] = (size_t)(h - cx->irs);
        }
    }

    size_t *index = arena_alloc(st->arena, n * sizeof(size_t), alignof(size_t));
    size_t *low = arena_alloc(st->arena, n * sizeof(size_t), alignof(size_t));
    size_t *scc_stack = arena_alloc(st->arena, n * sizeof(size_t), alignof(size_t));
    size_t *frame_node = arena_alloc(st->arena, n * sizeof(size_t), alignof(size_t));
    size_t *frame_edge = arena_alloc(st->arena, n * sizeof(size_t), alignof(size_t));
    bool *on_stack = arena_alloc(st->arena, n * sizeof(bool), alignof(bool));
    for (size_t i = 0; i < n; i++) {
        index[i] = SIZE_MAX;
        on_stack[i] = false;
    }
    size_t counter = 0, scc_top = 0, frames = 0;

    for (size_t root = 0; root < n; root++) {
        if (index[root] != SIZE_MAX) continue;
        index[root] = low[root] = counter++;
        scc_stack[scc_top++] = root;
        on_stack[root] = true;
        frame_node[0] = root;
        frame_edge[0] = adj_start[root];
        frames = 1;
        while (frames > 0) {
            size_t v = frame_node[frames - 1];
            if (frame_edge[frames - 1] < adj_start[v + 1]) {
                size_t w = adj[frame_edge[frames - 1]++];
                if (index[w] == SIZE_MAX) {
                    index[w] = low[w] = counter++;
                    scc_stack[scc_top++] = w;
                    on_stack[w] = true;
                    frame_node[frames] = w;
                    frame_edge[frames] = adj_start[w];
                    frames++;
                } else if (on_stack[w] && index[w] < low[v]) {
                    low[v] = index[w];
                }
                continue;
            }
            frames--;
            if (frames > 0 && low[v] < low[frame_node[frames - 1]]) low[frame_node[frames - 1]] = low[v];
            if (low[v] != index[v]) continue;

            /* v roots a component: its members are scc_stack from v's position up. */
            size_t base = scc_top;
            while (scc_stack[base - 1] != v) base--;
            base--;
            size_t size = scc_top - base;
            bool cyclic = size > 1;
            for (size_t e = adj_start[v]; size == 1 && e < adj_start[v + 1]; e++) cyclic = cyclic || adj[e] == v;
            if (cyclic) {
                size_t lowest = scc_stack[base];
                for (size_t k = base; k < scc_top; k++) {
                    cx->structs[scc_stack[k]].cyclic = true;
                    if (scc_stack[k] < lowest) lowest = scc_stack[k];
                }
                cx->structs[lowest].cycle_error = true;
            } else {
                layout_struct(cx, &cx->structs[v]);
            }
            for (size_t k = base; k < scc_top; k++) on_stack[scc_stack[k]] = false;
            scc_top = base;
        }
    }
}

bool ir_declare_items(IrState *st, const NodeList *items, IrModule *m) {
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
    analyze_structs(&cx);

    /* E0221, then E0222, each in declaration order. */
    for (size_t i = 0; i < n; i++) {
        if (!cx.structs[i].cycle_error) continue;
        st->progress = cx.structs[i].decl->span;
        ir_error(st, diag_ir_struct_contains_itself(st->arena, cx.structs[i].decl->as.struct_decl.name_span,
                                                    cx.structs[i].ir->name));
    }
    for (size_t i = 0; i < n; i++) {
        if (!cx.structs[i].size_error) continue;
        st->progress = cx.structs[i].decl->span;
        ir_error(st, diag_ir_type_too_large(cx.structs[i].size_error_span));
    }

    if (st->error_count != errors_before) return false;
    for (size_t i = 0; i < n; i++) node_list_push(st->arena, &m->structs, cx.structs[i].ir);
    return true;
}
