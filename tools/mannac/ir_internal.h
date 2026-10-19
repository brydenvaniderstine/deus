#pragma once
#include <stdalign.h>
#include <stdint.h>
#include <string.h>
#include "ir.h"

/* Attributes for keeping the frames of the recursive checker small (docs/TYPED_IR.md step 9, the
 * stack trend): IR_NOINLINE keeps a function's locals out of its caller's frame; IR_COLD is the
 * same for an error path. No effect on behavior; other compilers just ignore them. */
#if defined(__GNUC__) || defined(__clang__)
#define IR_NOINLINE __attribute__((noinline))
#define IR_COLD     __attribute__((noinline, cold))
#else
#define IR_NOINLINE
#define IR_COLD
#endif

/* Shared between the declaration pass (ir_decls.c), the expression checker (ir_expr.c) and
 * the statement builder (ir_body.c); not a public interface. */

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

static inline uint64_t name_hash(const char *s) {
    uint64_t h = 1469598103934665603ULL; /* FNV-1a */
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

/* Capacity for `n` entries at load <= 1/2 (so a lookup always finds an empty slot). */
static inline void symtab_init(IrState *st, Symtab *t, size_t n) {
    size_t cap = 2;
    while (cap < n * 2) cap *= 2;
    t->slots = arena_alloc(st->arena, cap * sizeof(SymEntry), alignof(SymEntry));
    memset(t->slots, 0, cap * sizeof(SymEntry));
    t->cap = cap;
}

static inline const SymEntry *symtab_lookup(const Symtab *t, const char *name) {
    size_t mask = t->cap - 1;
    for (size_t i = (size_t)(name_hash(name) & mask);; i = (i + 1) & mask) {
        if (!t->slots[i].name) return NULL;
        if (strcmp(t->slots[i].name, name) == 0) return &t->slots[i];
    }
}

/* Inserts unless the name is present; returns the entry that holds the name
 * (the new one, or the first declaration's). The table is sized for its keys
 * up front, so it can never fill. */
static inline const SymEntry *symtab_insert(Symtab *t, const char *name, Span name_span, bool is_struct, size_t index, bool *inserted) {
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

typedef struct CallEdge {
    uint32_t caller, callee; /* function positions in declaration order */
    Span     span;           /* the whole call */
} CallEdge;

typedef struct {
    IrState    *st;
    Symtab      top;     /* structs and functions, one namespace */
    StructInfo *structs; /* declaration order */
    size_t      n_structs;
    IrStruct   *irs;     /* contiguous: struct i is irs[i] */
    IrFunc    **fns;     /* signatures, in declaration order (bodies are ir_build's) */
    size_t      n_fns;
    /* The call graph, recorded while the bodies are built: one edge per call that names a function
     * (a SymEntry of a function holds its position in fns[] as `index`). Caller order = body order. */
    struct CallEdge *edges;
    size_t      n_edges, cap_edges;
} Ctx;

/* Where a type is being written, which decides what a slice may do there
 * (docs/M2_PLAN.md "Arrays, slices, storage"). */
typedef enum {
    CTX_FIELD,       /* a struct field: no slice at any depth */
    CTX_PARAM,       /* a parameter: a slice is fine (also inside arrays) */
    CTX_RETURN,      /* a return type: no slice at any depth */
    CTX_SLICE_ELEM,  /* inside a slice's element type: no slice at any depth */
} TypeCtx;

/* AST type -> IR type (ir_decls.c). A failure records ONE error and yields ERROR. */
const IrType *ir_resolve_type(Ctx *cx, const Type *t, TypeCtx ctx);
/* E0222 if the type's size exceeds the limit (silent for ERROR and for a type that needs an
 * unsized struct). */
void ir_check_type_size(Ctx *cx, const Type *ast, const IrType *t);

/* ---- naming conventions (ir_names.c, typed-IR step 14) ---- */
bool ir_is_snake_case(const char *name);
bool ir_is_pascal_case(const char *name);
/* The name rewritten in the convention, or NULL when no valid name can be made (empty after dropping
 * underscores, or it would start with a digit). Arena-allocated. */
const char *ir_to_snake_case(Arena *a, const char *name);
const char *ir_to_pascal_case(Arena *a, const char *name);
/* A lexer keyword or the builtin `len`: a name a rename must not produce. */
bool ir_is_reserved_name(const char *name);

/* The type of a primitive keyword (`u8`..`i64`, `bool`), or NULL (ir_decls.c). */
const IrType *ir_prim_type(IrState *st, const char *name);

/* True if a type built from an expression (an array literal, `[v; N]`) is larger than the limit
 * (E0222 at the expression; the type written in a declaration is checked by ir_check_type_size). */
bool ir_type_exceeds_limit(const IrType *t);

/* The top-level declaration pass: declares every struct and function name (E0210), builds each
 * struct's fields (E0211), resolves their types (E0220, E0227), analyzes containment and layout
 * (E0221, E0222) and checks every function signature (E0212, E0214, E0215, E0220, E0222, E0227).
 * Returns false if it recorded any error; otherwise *out_ctx is the context the body pass reads
 * (the top-level names, the structs, the function signatures). */
bool ir_declare_items(IrState *st, const NodeList *items, IrModule *m, Ctx **out_ctx);

/* ---- function bodies (typed-IR step 7) ---- */

/* The names visible in one function body. Parameters live in the function scope and are never
 * popped; each block pushes a scope and pops it at its end. There is NO shadowing (docs/M2_PLAN.md
 * "Names, scopes, items"): a name is visible at most once, so one table keyed by name is enough.
 * Entries are never removed: popping a scope marks its locals dead, and a later declaration of
 * the same name (a sibling block) revives the entry. Storage is not scoped: every local keeps its
 * own slot until the function returns (func->locals only grows). */
typedef struct {
    const char *name; /* NULL = empty slot */
    IrLocal    *local;
    bool        alive;
} VisEntry;

typedef struct {
    IrState  *st;
    Ctx      *decls;
    IrFunc   *func;
    VisEntry *tab;
    size_t    cap;       /* power of two */
    size_t    count;     /* distinct names ever declared */
    size_t   *scope_start; /* func->locals.len when each open block was entered */
    size_t    depth;     /* open blocks */
    size_t    scope_cap;
    size_t    fn_index;  /* this function's position among the functions (the call graph's caller) */
    size_t    loop_depth; /* enclosing `while`/`for` bodies: `break`/`continue` are legal only inside one */
} BodyCtx;

/* Starts a body: registers the parameters (already in func->locals, slots 0..n_params-1). */
void body_init(BodyCtx *bc, IrState *st, Ctx *decls, IrFunc *func);
void body_push_scope(BodyCtx *bc);
void body_pop_scope(BodyCtx *bc);
/* The visible local named `name`, or NULL. */
IrLocal *body_lookup(const BodyCtx *bc, const char *name);
/* Declares a local in the innermost scope: next slot, appended to func->locals. The caller has
 * already checked that the name is not visible. */
IrLocal *body_declare(BodyCtx *bc, const char *name, const IrType *type, Span span);

/* Builds a function body (a block); statements the later steps own stop the build. */
IrStmt *ir_build_body(IrState *st, Ctx *decls, IrFunc *f, size_t fn_index, const Stmt *body);

/* After every body: rejects direct and mutual recursion (E0235, once per cycle; ir_calls.c). */
void ir_check_recursion(IrState *st, Ctx *decls);

/* Checks `e` against `expected` (never NULL; ERROR means "the expected type already failed":
 * check the expression's own names, report nothing about its type). Errors are recorded and
 * the result is typed ERROR where the type could not be determined. */
IrExpr *ir_check_expr(BodyCtx *bc, const Expr *e, const IrType *expected);
/* The type of `e` with no expectation: bool literals and names; an integer literal expression
 * has no type of its own (E0225). */
IrExpr *ir_infer_expr(BodyCtx *bc, const Expr *e);
/* As ir_infer_expr, but a `unit` value is fine: an expression STATEMENT may be a call to a function
 * that returns nothing. */
IrExpr *ir_infer_discard(BodyCtx *bc, const Expr *e);
/* Checks `e` for its OWN mistakes only (names, operators, nested calls) and says nothing about its type
 * or about being a unit value: for a value whose expected type is unknown or irrelevant (the value of an
 * assignment to a non-lvalue, an unexpected return value, the arguments of an unknown function). */
void ir_check_silent(BodyCtx *bc, const Expr *e);
