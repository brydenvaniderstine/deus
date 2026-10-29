#include "ir_internal.h"
#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

/* Top-level declarations (typed-IR step 4, docs/M2_PLAN.md "Names, scopes,
 * items" and "Types"): the one top-level namespace (structs and functions),
 * duplicate names, struct fields, and type resolution. Diagnostics come out
 * grouped by check, each group in declaration order (E0210 for every
 * declaration, then E0211/E0220/E0227 struct by struct, ...). */



static const char *decl_name(const Decl *d) {
    return d->kind == DECL_STRUCT ? d->as.struct_decl.name : d->as.function_decl.name;
}

static Span decl_name_span(const Decl *d) {
    return d->kind == DECL_STRUCT ? d->as.struct_decl.name_span : d->as.function_decl.name_span;
}

/* The IR type of a primitive keyword (`u8`..`i64`, `bool`), or NULL for any other name. */
const IrType *ir_prim_type(IrState *st, const char *name) {
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
 * every enclosing array or slice silently propagates (one mistake, one error).
 * Recursion is once per nested array/slice level: the parser bounds that to 254. */
#ifdef MANNA_POISON_INJECT
/* The third injection point (ir.h): every resolved type, the recursive ones too, passes through the wrapper. Normal builds have no wrapper. */
static const IrType *resolve_type_impl(Ctx *cx, const Type *t, TypeCtx ctx);
const IrType *ir_resolve_type(Ctx *cx, const Type *t, TypeCtx ctx) {
    return IR_INJECT_TYPE(cx->st, resolve_type_impl(cx, t, ctx), IR_INJ_DECL_TYPE, (int)t->kind);
}
static const IrType *resolve_type_impl(Ctx *cx, const Type *t, TypeCtx ctx) {
#else
const IrType *ir_resolve_type(Ctx *cx, const Type *t, TypeCtx ctx) {
#endif
    IrState *st = cx->st;
    switch (t->kind) {
        case TYPE_IDENT: {
            const IrType *p = ir_prim_type(st, t->as.ident.name);
            if (p) return p;
            const SymEntry *e = symtab_lookup(&cx->top, t->as.ident.name);
            if (e && e->is_struct) {
                if (cx->struct_renames) ir_note_use(st, cx->struct_renames[e->index], t->span);
                return ir_type_struct(st, &cx->irs[e->index]);
            }
            /* A function named in a type position resolves to nothing (E0220), but the spelling is still the function's name: a rename of the
             * function reaches it, so the fixed source says the same thing as before (re-audit 3, finding 6, r1). */
            if (e && cx->fn_renames) ir_note_use(st, cx->fn_renames[e->index], t->span);
            ir_note_unresolved(st, cx, NS_TOP, 0, t->as.ident.name);
            ir_error(st, diag_ir_unknown_type(st->arena, t->span, t->as.ident.name));
            return ir_type_error(st);
        }
        case TYPE_ARRAY: {
            const IrType *elem = ir_resolve_type(cx, t->as.array.elem, ctx);
            if (elem->kind == IR_T_ERROR) return elem;
            return ir_type_array(st, t->as.array.size, elem);
        }
        case TYPE_SLICE:
            break;
    }
    /* A slice. */
    if (ctx == CTX_PARAM) {
        const IrType *elem = ir_resolve_type(cx, t->as.slice.elem, CTX_SLICE_ELEM);
        return elem->kind == IR_T_ERROR ? elem : ir_type_slice(st, elem);
    }
    ir_error(st, ctx == CTX_FIELD ? diag_ir_slice_in_field(t->span)
                 : ctx == CTX_RETURN ? diag_ir_slice_in_return(t->span)
                                     : diag_ir_slice_of_slice(t->span));
    return ir_type_error(st);
}

/* Pass 1: every top-level name into the one namespace. The first declaration
 * of a name wins; each later one is E0210 at its name, naming the first. */
static void declare_names(Ctx *cx, const NodeList *items) {
    IrState *st = cx->st;
    size_t si = 0, fi = 0; /* positions among the structs and among the functions, in declaration order */
    for (size_t i = 0; i < items->len; i++) {
        const Decl *d = items->items[i];
        st->progress = d->span;
        bool is_struct = d->kind == DECL_STRUCT;
        if (is_struct) {
            cx->structs[si].decl = d;
            cx->structs[si].ir = &cx->irs[si];
            cx->irs[si].serial = (uint32_t)si; /* before any type that names this struct can be interned (a forward reference) */
        }
        bool inserted;
        const SymEntry *e = symtab_insert(&cx->top, decl_name(d), decl_name_span(d), is_struct, is_struct ? si : fi, &inserted);
        if (!inserted) ir_error(st, diag_ir_duplicate_decl(st->arena, decl_name_span(d), decl_name(d), e->name_span));
        if (strcmp(decl_name(d), "len") == 0) ir_error(st, diag_ir_len_declared(decl_name_span(d))); /* the builtin (Admission #15) */
        else if (inserted) { /* a duplicate binds nothing: not also checked */
            Rename *r = ir_check_naming(st, cx, REN_TOP, 0, is_struct, decl_name(d), decl_name_span(d));
            if (r) (is_struct ? ir_struct_rename_table(st, cx) : ir_fn_rename_table(st, cx))[is_struct ? si : fi] = r;
        }
        if (is_struct) si++; else fi++;
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
    uint32_t serial = ir->serial;
    memset(ir, 0, sizeof *ir);
    ir->serial = serial;
    ir->name = d->as.struct_decl.name;
    ir->span = d->span;
    ir->align = 1;
    ir->fields = arena_alloc(st->arena, (fl->len ? fl->len : 1) * sizeof(IrField), alignof(IrField));
    s->field_types = arena_alloc(st->arena, (fl->len ? fl->len : 1) * sizeof(const Type *), alignof(const Type *));

    Symtab seen = {0};
    if (fl->len > 0) symtab_init(st, &seen, fl->len);
    for (size_t i = 0; i < fl->len; i++) {
        const Field *f = fl->items[i];
        bool inserted;
        symtab_insert(&seen, f->name, f->name_span, false, 0, &inserted);
        if (!inserted) ir_error(st, diag_ir_duplicate_field(st->arena, f->name_span, f->name, ir->name));
        else {
            Rename *r = ir_check_naming(st, cx, REN_FIELD, (size_t)(s - cx->structs), false, f->name, f->name_span);
            if (r) {
                if (!s->field_renames) {
                    s->field_renames = arena_alloc(st->arena, fl->len * sizeof(Rename *), alignof(Rename *));
                    for (size_t k = 0; k < fl->len; k++) s->field_renames[k] = NULL;
                }
                s->field_renames[ir->n_fields] = r; /* the field about to be kept at this position */
            }
        }
        const IrType *t = ir_resolve_type(cx, f->type, CTX_FIELD);
        if (t->kind == IR_T_ERROR) s->poisoned = true;
        if (!inserted) {
            /* a repeated field is not kept, but a type that is too large is a mistake of its own: layout_struct looks at it (a type that failed to resolve has no size and is skipped there) */
            if (!s->dup_types) {
                s->dup_types = arena_alloc(st->arena, fl->len * sizeof(const Type *), alignof(const Type *));
                s->dup_ir = arena_alloc(st->arena, fl->len * sizeof(const IrType *), alignof(const IrType *));
            }
            s->dup_types[s->n_dup] = f->type;
            s->dup_ir[s->n_dup++] = t;
            continue;
        }
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
        case IR_T_SLICE: {
            /* For the SIZE CHECK only a slice counts as 16 bytes, align 8 (pointer + length
             * at 64 bits, the larger of the Wasm and native targets), so a type that passes
             * the limit fits on every backend; the real layout is the backend's choice
             * (docs/M2_PLAN.md "Layout and limits"). Its element type is still checked. */
            uint64_t es;
            uint32_t ea;
            LayResult r = type_layout(cx, ast->as.slice.elem, t->as.slice.elem, &es, &ea, bad);
            if (r != LAY_OK) return r;
            *size = 16;
            *align = 8;
            return LAY_OK;
        }
        default:
            break;
    }
    /* A struct: unit and ERROR never reach here (unit is unwritable; a failed type is
     * never laid out: layout_struct skips a field whose type failed, and a failed array or
     * slice type is ERROR as a whole). */
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

static void add_size_span(Ctx *cx, StructInfo *s, Span at) {
    if (s->n_size_spans == s->cap_size_spans) {
        size_t cap = s->cap_size_spans ? s->cap_size_spans * 2 : 4;
        Span *list = arena_alloc(cx->st->arena, cap * sizeof(Span), alignof(Span));
        if (s->n_size_spans) memcpy(list, s->size_spans, s->n_size_spans * sizeof(Span));
        s->size_spans = list;
        s->cap_size_spans = cap;
    }
    s->size_spans[s->n_size_spans++] = at;
}

/* Fields in declaration order, natural alignment per field, trailing padding to the
 * struct's alignment, no reordering (docs/M2_PLAN.md "Layout and limits"). All
 * intermediate values stay below 2^32 because each is checked against the limit
 * before the next addition, so nothing wraps.
 *
 * Called for EVERY struct, also one in a cycle or with a field that failed to resolve (step 22d, round-4 finding 4, Bryden 2026-10-29): such a struct gets no layout, but a field whose
 * OWN type is too large is a mistake of its own and is always reported (E0222 at that type, one per field; the first used to end the look at the others, and a struct in error was not
 * looked at). A field whose size is unknown (unsized struct, failed type) is skipped. The TOTAL is checked only while every field so far has a known size: the offsets after a field
 * whose size is unknown (or too large) are not known, so an overflow there is not claimed (it was, as a lower bound, until step 22d). */
static void layout_struct(Ctx *cx, StructInfo *s) {
    IrStruct *ir = s->ir;
    uint64_t off = 0;
    uint32_t max_align = 1;
    bool unknown = false; /* the struct will have no layout */
    if (s->poisoned) unknown = true; /* a repeated field's type failed (a kept field's type is looked at below) */
    bool counting = true;                    /* every field so far has a known size */
    for (size_t i = 0; i < ir->n_fields; i++) {
        uint64_t fs = 0;
        uint32_t fa = 0;
        Span bad;
        if (ir->fields[i].type->kind == IR_T_ERROR) { /* no size: the mistake is reported where the type was resolved */
            unknown = true;
            counting = false;
            continue;
        }
        LayResult r = type_layout(cx, s->field_types[i], ir->fields[i].type, &fs, &fa, &bad);
        if (r == LAY_UNSIZED) {
            unknown = true;
            counting = false;
            continue;
        }
        if (r == LAY_TOO_BIG) {
            add_size_span(cx, s, bad);
            unknown = true;
            counting = false;
            continue;
        }
        if (!counting) continue;
        off = align_up(off, fa);
        ir->fields[i].offset = off;
        off += fs;
        if (fa > max_align) max_align = fa;
        if (off > IR_MAX_TYPE_SIZE) {
            add_size_span(cx, s, s->decl->as.struct_decl.name_span);
            unknown = true;
            counting = false;
        }
    }
    for (size_t i = 0; i < s->n_dup; i++) {
        uint64_t fs;
        uint32_t fa;
        Span bad;
        if (s->dup_ir[i]->kind == IR_T_ERROR) continue; /* a repeated field whose type failed has no size (it marked the struct poisoned) */
        if (type_layout(cx, s->dup_types[i], s->dup_ir[i], &fs, &fa, &bad) == LAY_TOO_BIG) add_size_span(cx, s, bad);
    }
    if (counting && align_up(off, max_align) > IR_MAX_TYPE_SIZE) {
        add_size_span(cx, s, s->decl->as.struct_decl.name_span);
        unknown = true;
    }
    if (unknown) return;
    ir->size = align_up(off, max_align);
    ir->align = max_align;
    s->sized = true;
}

static int cmp_span_pos(const void *pa, const void *pb) {
    const Span *a = pa, *b = pb;
    if (a->start_line != b->start_line) return a->start_line < b->start_line ? -1 : 1;
    if (a->start_col != b->start_col) return a->start_col < b->start_col ? -1 : 1;
    return 0;
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
                for (size_t k = base; k < scc_top; k++) layout_struct(cx, &cx->structs[scc_stack[k]]); /* no layout, but the fields' own size mistakes (step 22d) */
            } else {
                layout_struct(cx, &cx->structs[v]);
            }
            for (size_t k = base; k < scc_top; k++) on_stack[scc_stack[k]] = false;
            scc_top = base;
        }
    }
}

/* ---- function signatures ---- */

/* E0222 for a parameter, return or `let` type whose size exceeds the limit; true if it was reported,
 * so a `let` can poison its annotation (one mistake, one error). A type that needs an unsized struct
 * is silent (that struct's own error is already reported). */
bool ir_check_type_size(Ctx *cx, const Type *ast, const IrType *t) {
    if (t->kind == IR_T_ERROR) return false;
    uint64_t size;
    uint32_t align;
    Span bad;
    if (type_layout(cx, ast, t, &size, &align, &bad) == LAY_TOO_BIG) {
        ir_error(cx->st, diag_ir_type_too_large(bad));
        return true;
    }
    return false;
}

/* Size of a type built from an EXPRESSION (an array literal, `[v; N]`): false if it exceeds the
 * limit, including by overflow. Every struct has its layout by the time bodies are checked. Recursion
 * is once per nested array level (bounded by the parser's nesting bound). */
static bool type_size_in_limit(const IrType *t, uint64_t *size) {
    switch (t->kind) {
        case IR_T_INT:    *size = t->as.integer.bits / 8; return true;
        case IR_T_BOOL:   *size = 1; return true;
        case IR_T_SLICE:  *size = 16; return true; /* the size-check convention, "Layout and limits" */
        case IR_T_STRUCT: *size = t->as.strukt.decl->size; return true;
        case IR_T_ARRAY: {
            uint64_t es;
            if (!type_size_in_limit(t->as.array.elem, &es)) return false;
            if (es == 0 || t->as.array.len == 0) { *size = 0; return true; }
            if (t->as.array.len > IR_MAX_TYPE_SIZE / es) return false;
            *size = t->as.array.len * es;
            return true;
        }
        default:          *size = 0; return true;
    }
}

bool ir_type_exceeds_limit(const IrType *t) {
    uint64_t size;
    return !type_size_in_limit(t, &size);
}

/* One function's signature: attributes, then parameters in order, then the return
 * type. The first `kernel_hook` sets the flag; a repeat is E0215 (with a fix that
 * deletes it); any other name is E0214 and gets no further check, so `[Kernel_hook]`
 * is one error, not also a casing error. A repeated parameter name is E0212 (the first
 * wins; the repeat's type is still resolved for its own errors, but it is not a local).
 * A parameter whose type failed still declares its name, typed ERROR, so later uses do
 * not cascade. */
static void build_signature(Ctx *cx, const Decl *d) {
    IrState *st = cx->st;
    const NodeList *attrs = &d->as.function_decl.attributes;
    const NodeList *params = &d->as.function_decl.params;
    st->progress = d->span;

    bool kernel_hook = false;
    for (size_t i = 0; i < attrs->len; i++) {
        const Attribute *a = attrs->items[i];
        if (strcmp(a->name, "kernel_hook") != 0) {
            ir_error(st, diag_ir_unknown_attribute(st->arena, a->name_span, a->name));
        } else if (kernel_hook) {
            ir_error(st, diag_ir_duplicate_attribute(st->arena, a->name_span, a->span, a->name));
        } else {
            kernel_hook = true;
        }
    }

    NodeList locals;
    node_list_init(&locals);
    Symtab seen = {0};
    if (params->len > 0) symtab_init(st, &seen, params->len);
    for (size_t i = 0; i < params->len; i++) {
        const Param *p = params->items[i];
        bool inserted;
        symtab_insert(&seen, p->name, p->name_span, false, 0, &inserted);
        if (!inserted) ir_error(st, diag_ir_duplicate_param(st->arena, p->name_span, p->name));
        if (strcmp(p->name, "len") == 0) ir_error(st, diag_ir_len_declared(p->name_span));
        else if (inserted) {
            Rename *r = ir_check_naming(st, cx, REN_LOCAL, cx->n_fns, false, p->name, p->name_span);
            if (r) {
                if (!cx->param_renames) {
                    cx->param_renames = arena_alloc(st->arena, (cx->cap_decl_items ? cx->cap_decl_items : 1) * sizeof(Rename **), alignof(Rename **));
                    for (size_t k = 0; k < cx->cap_decl_items; k++) cx->param_renames[k] = NULL;
                }
                if (!cx->param_renames[cx->n_fns]) {
                    cx->param_renames[cx->n_fns] = arena_alloc(st->arena, params->len * sizeof(Rename *), alignof(Rename *));
                    for (size_t k = 0; k < params->len; k++) cx->param_renames[cx->n_fns][k] = NULL;
                }
                cx->param_renames[cx->n_fns][locals.len] = r; /* the parameter about to take this slot */
            }
        }
        const IrType *t = ir_resolve_type(cx, p->type, CTX_PARAM);
        ir_check_type_size(cx, p->type, t);
        if (!inserted) continue;
        node_list_push(st->arena, &locals, ir_local_new(st, (uint32_t)locals.len, p->name, t, p->span, true));
    }
    size_t n_params = locals.len;

    const IrType *ret = ir_type_unit(st);
    if (d->as.function_decl.return_type) {
        ret = ir_resolve_type(cx, d->as.function_decl.return_type, CTX_RETURN);
        ir_check_type_size(cx, d->as.function_decl.return_type, ret);
    }
    cx->fns[cx->n_fns++] = ir_func_new(st, d->span, d->as.function_decl.name, kernel_hook, locals, n_params, ret, NULL);
}

bool ir_declare_items(IrState *st, const NodeList *items, IrModule *m, Ctx **out_ctx) {
    int errors_before = st->error_count;
    Ctx *cxp = arena_alloc(st->arena, sizeof(Ctx), alignof(Ctx));
    Ctx cx = {0};
    cx.st = st;
    size_t n = 0;
    for (size_t i = 0; i < items->len; i++) n += ((const Decl *)items->items[i])->kind == DECL_STRUCT;
    cx.n_structs = n;
    cx.cap_decl_items = items->len;
    symtab_init(st, &cx.top, items->len);
    cx.structs = arena_alloc(st->arena, (n ? n : 1) * sizeof(StructInfo), alignof(StructInfo));
    cx.irs = arena_alloc(st->arena, (n ? n : 1) * sizeof(IrStruct), alignof(IrStruct));
    cx.fns = arena_alloc(st->arena, (items->len - n ? items->len - n : 1) * sizeof(IrFunc *), alignof(IrFunc *));
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
        StructInfo *si = &cx.structs[i];
        if (si->n_size_spans > 1) qsort(si->size_spans, si->n_size_spans, sizeof(Span), cmp_span_pos);
        for (size_t k = 0; k < si->n_size_spans; k++) {
            st->progress = si->decl->span;
            ir_error(st, diag_ir_type_too_large(si->size_spans[k]));
        }
    }

    for (size_t i = 0; i < items->len; i++) {
        const Decl *d = items->items[i];
        if (d->kind == DECL_FUNCTION) build_signature(&cx, d);
    }

    /* Naming errors (E0280/E0281) bind everything normally, so they alone do not stop the build: the bodies are
     * still checked, which also finds every use of a misnamed declaration. Any other error stops it here (and
     * `len` named by a struct, function or parameter is one of them, which the bare-`len` assert relies on). */
    if (st->error_count - cx.naming_errors != errors_before) return false;
    for (size_t i = 0; i < n; i++) node_list_push(st->arena, &m->structs, cx.structs[i].ir);
    for (size_t i = 0; i < cx.n_fns; i++) node_list_push(st->arena, &m->funcs, cx.fns[i]);
    *cxp = cx;
    *out_ctx = cxp;
    return true;
}
