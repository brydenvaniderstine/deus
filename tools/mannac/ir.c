#include "ir.h"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

/* ---- primitives ---- */

/* Slot order of IrState.prim. */
enum { P_U8, P_I8, P_U16, P_I16, P_U32, P_I32, P_U64, P_I64, P_BOOL, P_UNIT, P_ERROR };
_Static_assert(P_ERROR + 1 == IR_N_PRIM, "IR_N_PRIM must match the slot enum");

static void prims_init(IrState *st) {
    for (int i = 0; i < 8; i++) {
        IrType *t = &st->prim[i];
        memset(t, 0, sizeof(*t));
        t->kind = IR_T_INT;
        t->as.integer.bits = (uint8_t)(8 << (i / 2));
        t->as.integer.is_signed = (i % 2) == 1;
    }
    memset(&st->prim[P_BOOL], 0, sizeof(IrType));
    st->prim[P_BOOL].kind = IR_T_BOOL;
    memset(&st->prim[P_UNIT], 0, sizeof(IrType));
    st->prim[P_UNIT].kind = IR_T_UNIT;
    memset(&st->prim[P_ERROR], 0, sizeof(IrType));
    st->prim[P_ERROR].kind = IR_T_ERROR;
}

/* ---- state ---- */

void ir_state_reset_for_build(IrState *st) {
    st->error_count = 0;
    st->progress = (Span){1, 1, 1, 1};
    arena_rearm_overflow_hook(st->arena); /* E0201 again if this build overflows too */
    prims_init(st);
    st->composites.slots = NULL;
    st->composites.cap = 0;
    st->composites.count = 0;
}

/* The hook runs just before the overflow unwind and records E0201 in the
 * arena's reserved tail, so the unwind is never silent (the parser's E0110
 * mechanism). */
static void on_arena_overflow(void *ctx) {
    IrState *st = ctx;
    diag_add(st->arena, &st->diags, diag_ir_input_too_large(st->progress));
}

void ir_state_init(IrState *st, size_t arena_max_size) {
    st->arena = arena_create(arena_max_size);
    diag_list_init(&st->diags);
    ir_state_reset_for_build(st);
    arena_set_overflow_target(st->arena, &st->fatal_target);
    /* Room for the E0201 diagnostic and the growth of the list it joins. */
    size_t reserve = arena_max_size >= (1u << 20) ? (16u << 10) : arena_max_size / 4;
    arena_set_overflow_hook(st->arena, on_arena_overflow, st, reserve);
}

void ir_state_destroy(IrState *st) {
    arena_destroy(st->arena);
}

void ir_error(IrState *st, Diagnostic d) {
    diag_add(st->arena, &st->diags, d);
    st->error_count++;
    if (st->error_count >= IR_MAX_ERRORS) {
        diag_add(st->arena, &st->diags, diag_ir_too_many_errors(d.span));
        longjmp(st->fatal_target, 1);
    }
}

/* ---- interning ---- */

const IrType *ir_type_int(IrState *st, int bits, bool is_signed) {
    int idx;
    switch (bits) {
        case 8:  idx = 0; break;
        case 16: idx = 1; break;
        case 32: idx = 2; break;
        case 64: idx = 3; break;
        default: assert(!"ir_type_int: bits must be 8, 16, 32 or 64"); idx = 0; break;
    }
    return &st->prim[idx * 2 + (is_signed ? 1 : 0)];
}

const IrType *ir_type_bool(IrState *st)  { return &st->prim[P_BOOL]; }
const IrType *ir_type_unit(IrState *st)  { return &st->prim[P_UNIT]; }
const IrType *ir_type_error(IrState *st) { return &st->prim[P_ERROR]; }

static uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

/* Structural key of a composite: its kind plus (len, elem) or (decl). Element
 * pointers are themselves canonical, so equal keys <=> equal types. */
static uint64_t type_hash(IrTypeKind kind, uint64_t a, const void *p) {
    uint64_t h = mix(0, (uint64_t)kind);
    h = mix(h, a);
    return mix(h, (uint64_t)(uintptr_t)p);
}

static bool type_matches(const IrType *t, IrTypeKind kind, uint64_t a, const void *p) {
    if (t->kind != kind) return false;
    switch (kind) {
        case IR_T_ARRAY:  return t->as.array.len == a && t->as.array.elem == p;
        case IR_T_SLICE:  return t->as.slice.elem == p;
        case IR_T_STRUCT: return t->as.strukt.decl == p;
        default:          return false;
    }
}

static void table_grow(IrState *st) {
    IrTypeTable *tb = &st->composites;
    size_t new_cap = tb->cap == 0 ? 64 : tb->cap * 2;
    const IrType **slots = arena_alloc(st->arena, new_cap * sizeof(*slots), alignof(const IrType *));
    memset(slots, 0, new_cap * sizeof(*slots));
    for (size_t i = 0; i < tb->cap; i++) {
        const IrType *t = tb->slots[i];
        if (!t) continue;
        uint64_t a = 0;
        const void *p = NULL;
        if (t->kind == IR_T_ARRAY)       { a = t->as.array.len; p = t->as.array.elem; }
        else if (t->kind == IR_T_SLICE)  { p = t->as.slice.elem; }
        else                             { p = t->as.strukt.decl; }
        size_t j = (size_t)(type_hash(t->kind, a, p) & (new_cap - 1));
        while (slots[j]) j = (j + 1) & (new_cap - 1);
        slots[j] = t;
    }
    /* Installed only after every allocation succeeded: a longjmp out of
     * arena_alloc above leaves the old table intact. */
    tb->slots = slots;
    tb->cap = new_cap;
}

static const IrType *intern(IrState *st, IrTypeKind kind, uint64_t a, const void *p) {
    IrTypeTable *tb = &st->composites;
    if (tb->cap == 0 || (tb->count + 1) * 2 > tb->cap) table_grow(st);
    size_t mask = tb->cap - 1;
    size_t i = (size_t)(type_hash(kind, a, p) & mask);
    while (tb->slots[i]) {
        if (type_matches(tb->slots[i], kind, a, p)) return tb->slots[i];
        i = (i + 1) & mask;
    }
    IrType *t = arena_alloc(st->arena, sizeof(IrType), alignof(IrType));
    memset(t, 0, sizeof(*t));
    t->kind = kind;
    if (kind == IR_T_ARRAY)       { t->as.array.len = a; t->as.array.elem = p; }
    else if (kind == IR_T_SLICE)  { t->as.slice.elem = p; }
    else                          { t->as.strukt.decl = p; }
    tb->slots[i] = t;
    tb->count++;
    return t;
}

const IrType *ir_type_array(IrState *st, uint64_t len, const IrType *elem) {
    return intern(st, IR_T_ARRAY, len, elem);
}

const IrType *ir_type_slice(IrState *st, const IrType *elem) {
    return intern(st, IR_T_SLICE, 0, elem);
}

const IrType *ir_type_struct(IrState *st, const IrStruct *decl) {
    return intern(st, IR_T_STRUCT, 0, decl);
}

const char *ir_type_text(IrState *st, const IrType *t) {
    static const char *const int_names[8] = {"u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64"};
    switch (t->kind) {
        case IR_T_INT: {
            int idx = (t->as.integer.bits == 8 ? 0 : t->as.integer.bits == 16 ? 1 : t->as.integer.bits == 32 ? 2 : 3);
            return int_names[idx * 2 + (t->as.integer.is_signed ? 1 : 0)];
        }
        case IR_T_BOOL:   return "bool";
        case IR_T_UNIT:   return "unit";
        case IR_T_ERROR:  return "<error>";
        case IR_T_STRUCT: return t->as.strukt.decl->name;
        case IR_T_ARRAY:
            return arena_sprintf(st->arena, "[%llu]%s", (unsigned long long)t->as.array.len,
                                 ir_type_text(st, t->as.array.elem));
        case IR_T_SLICE:
            return arena_sprintf(st->arena, "[]%s", ir_type_text(st, t->as.slice.elem));
    }
    return "<error>"; /* unreachable: every IrTypeKind has a case */
}
