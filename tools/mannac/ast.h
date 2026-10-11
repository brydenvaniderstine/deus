#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "arena.h"

/* v0 AST (docs/M2_PLAN.md "AST node types"). All nodes are arena-allocated
 * (see arena.h) - nothing here is ever individually freed; the whole tree
 * dies with its arena. Every node carries a start-AND-end span, per
 * M2_PLAN.md's Language design decisions item 4 (needed for the
 * `manna check --json` error schema's precise ranges). */

typedef struct {
    int start_line, start_col;
    int end_line, end_col;
} Span;

/* Generic growable array of arena-allocated pointers, used for every list
 * in this AST (fields, params, args, statements, attributes) rather than
 * one hand-rolled growable-array type per list kind - one small, well-
 * tested implementation instead of five nearly-identical ones (principle
 * 7, small core, applied to this implementation, not just the language).
 * Doubles on growth, copying into a fresh arena allocation each time; the
 * abandoned smaller buffer is wasted space within the arena, not a leak -
 * bounded by the arena's own hard cap regardless. */
typedef struct {
    void   **items;
    size_t   len;
    size_t   cap;
} NodeList;

void node_list_init(NodeList *list);
void node_list_push(Arena *a, NodeList *list, void *item);

typedef struct Type Type;
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Decl Decl;

/* ---- Types: TypeIdent | TypeArray | TypeSlice ---- */

typedef enum { TYPE_IDENT, TYPE_ARRAY, TYPE_SLICE } TypeKind;

struct Type {
    TypeKind kind;
    Span     span;
    union {
        struct { const char *name; } ident;          /* arena-owned, NUL-terminated */
        struct { uint64_t size; Type *elem; } array;  /* [size]elem */
        struct { Type *elem; } slice;                 /* []elem */
    } as;
};

Type *type_ident(Arena *a, Span span, const char *name, size_t name_len);
Type *type_array(Arena *a, Span span, uint64_t size, Type *elem);
Type *type_slice(Arena *a, Span span, Type *elem);

/* ---- Declarations: StructDecl | FunctionDecl ---- */

typedef struct {
    Span        span; /* M2_PLAN.md's abstract Field/Param notation omits
                        * a span, but its own example golden dump shows
                        * one ("(param@2:8-2:17 a u32)") - following the
                        * blanket "every node records a span" rule and
                        * the concrete example over the abstract shorthand. */
    Span        name_span; /* the name token alone (the typed-IR stage points
                            * its naming/duplicate diagnostics here) */
    const char *name;
    Type       *type;
} Field; /* also used for function parameters - same shape */

typedef Field Param;

Field *field_new(Arena *a, Span span, Span name_span, const char *name, size_t name_len, Type *type);

/* One `[ IDENT ]` attribute on a function. `span` covers the brackets,
 * `name_span` just the identifier (what an unknown-attribute diagnostic
 * points at); `span` is what a duplicate-attribute fix would delete. */
typedef struct {
    Span        span;
    Span        name_span;
    const char *name; /* arena-owned, NUL-terminated */
} Attribute;

Attribute *attribute_new(Arena *a, Span span, Span name_span, const char *name, size_t name_len);

typedef enum { DECL_STRUCT, DECL_FUNCTION } DeclKind;

struct Decl {
    DeclKind kind;
    Span     span;
    union {
        struct {
            Span        name_span;
            const char *name;
            NodeList    fields;   /* Field* */
        } struct_decl;
        struct {
            NodeList    attributes; /* Attribute* - e.g. name "kernel_hook" */
            Span        name_span;
            const char *name;
            NodeList    params;     /* Param* */
            Type       *return_type; /* NULL = unit (no '-> type' present) */
            Stmt       *body;       /* always a STMT_BLOCK */
        } function_decl;
    } as;
};

Decl *decl_struct(Arena *a, Span span, Span name_span, const char *name, size_t name_len, NodeList fields);
Decl *decl_function(Arena *a, Span span, NodeList attributes, Span name_span, const char *name, size_t name_len,
                     NodeList params, Type *return_type, Stmt *body);

/* ---- Expressions ---- */

typedef enum {
    EXPR_NUMBER, EXPR_BOOL, EXPR_IDENT,
    EXPR_UNARY, EXPR_BINARY,
    EXPR_CALL, EXPR_INDEX, EXPR_SLICE, EXPR_FIELD_ACCESS,
    EXPR_CONVERT, /* u64(x): explicit integer conversion, #4 step 2 */
    EXPR_ARRAY, EXPR_ARRAY_REPEAT, /* [a, b, c] and [v; N], #4 step 3 */
    EXPR_NAMED_ARG,                /* x: 1 inside a call's args, #4 step 4 */
} ExprKind;

typedef enum { UNOP_NOT, UNOP_NEG, UNOP_BITNOT } UnOp;

typedef enum {
    BINOP_ADD, BINOP_SUB, BINOP_MUL, BINOP_DIV, BINOP_MOD,
    BINOP_BITAND, BINOP_BITOR, BINOP_BITXOR,
    BINOP_SHL, BINOP_SHR,
    BINOP_LT, BINOP_GT, BINOP_LE, BINOP_GE, BINOP_EQ, BINOP_NE,
    BINOP_AND, BINOP_OR,
} BinOp;

/* Upper bound on the height of any expression tree the PARSER builds (E0111;
 * docs/M2_PLAN.md "Typed IR (stage 3)" -> "Input contract"). Height counts
 * nodes along the longest root-to-leaf path (a leaf is 1; parentheses add
 * nothing, there is no Paren node), so it is exactly the host-stack recursion
 * depth any walker over the tree needs: the nesting-depth bound counts
 * recursion CYCLES, but left-associative binary chains and postfix chains are
 * built by loops, so without this bound a flat input makes a tree as tall as
 * it is long (docs/STATUS.md "Known issues"). Later stages ASSERT this bound,
 * they do not enforce one of their own.
 *
 * 512 is the MEASURED choice (tools/measure_expr_height.sh, table in
 * M2_PLAN.md next to E0111): the rule was N = largest power of two <=
 * (smallest tree height the AST printer survives under ASan+UBSan on an
 * 8 MB stack) / 8, floor 512. That height is 3,225, giving 403, so the floor
 * applies; 512 is also the smallest value that keeps every existing golden
 * valid (tallest: 511, depth_namedarg_255_ok). The effective margin is
 * 3,225 / 512 = 6.3x. IR step 3 must re-prove it with the real IR builder.
 * A -DMANNA_MAX_EXPR_HEIGHT=n override exists for MEASUREMENT builds only
 * (it disables the bound so the script can find where walkers fail). */
#ifdef MANNA_MAX_EXPR_HEIGHT
#define MAX_EXPR_HEIGHT MANNA_MAX_EXPR_HEIGHT
#else
#define MAX_EXPR_HEIGHT 512
#endif
_Static_assert(MAX_EXPR_HEIGHT < UINT16_MAX, "Expr.height is a uint16_t");

struct Expr {
    ExprKind kind;
    Span     span;
    /* True if this exact node was the immediate operand of at least one
     * '(' expression ')' grouping (docs/M2_PLAN.md records the IR-stage
     * consumer: only a literal directly negated, e.g. "-9223372036854775808"
     * with no parens in between, is the i64::MIN pattern - "-(2^63)" must
     * be a plain overflow error instead, and this flag is how that stage
     * tells the two apart without a dedicated Paren node, which the
     * grammar deliberately doesn't have). Set (and the span widened to
     * include the parens) in parse_primary's '(' case - see parser.c. */
    bool     parenthesized;
    /* Height of the tree rooted here (leaf = 1), computed by the constructors
     * from the children; saturates at UINT16_MAX instead of wrapping so a
     * tree built without the parser's E0111 guard can never look short. Not
     * printed by the dump. Sits in padding: sizeof(Expr) is unchanged. */
    uint16_t height;
    union {
        struct { uint64_t value; } number;
        struct { bool value; } boolean;
        struct { const char *name; } ident;
        struct { UnOp op; Expr *operand; } unary;
        struct { BinOp op; Expr *lhs; Expr *rhs; } binary;
        struct { Span name_span; const char *name; NodeList args; } call; /* args: Expr*; name_span: the callee identifier */
        struct { Expr *base; Expr *index; } index;
        struct { Expr *base; Expr *lo; Expr *hi; } slice;
        struct { Expr *base; Span name_span; const char *name; } field_access; /* name_span: the field identifier */
        struct { Span target_span; const char *target; Expr *operand; } convert; /* target: the type keyword's text; target_span: that keyword */
        struct { NodeList elems; } array;                      /* elems: Expr*, at least one */
        struct { Expr *value; uint64_t count; } array_repeat;  /* count: the NUMBER token's value */
        struct { Expr *name; Expr *value; } named_arg;         /* only ever an element of a call's args */
    } as;
};

Expr *expr_number(Arena *a, Span span, uint64_t value);
Expr *expr_bool(Arena *a, Span span, bool value);
Expr *expr_ident(Arena *a, Span span, const char *name, size_t name_len);
Expr *expr_unary(Arena *a, Span span, UnOp op, Expr *operand);
Expr *expr_binary(Arena *a, Span span, BinOp op, Expr *lhs, Expr *rhs);
Expr *expr_call(Arena *a, Span span, Span name_span, const char *name, size_t name_len, NodeList args);
Expr *expr_index(Arena *a, Span span, Expr *base, Expr *index);
Expr *expr_slice(Arena *a, Span span, Expr *base, Expr *lo, Expr *hi);
Expr *expr_field_access(Arena *a, Span span, Expr *base, Span name_span, const char *name, size_t name_len);
/* `target` is the primitive type keyword's text ("u64"); the parser accepts any
 * of the nine PRIM_TYPE keywords, the IR stage decides which conversions are legal. */
Expr *expr_convert(Arena *a, Span span, Span target_span, const char *target, size_t target_len, Expr *operand);
Expr *expr_array(Arena *a, Span span, NodeList elems);
Expr *expr_array_repeat(Arena *a, Span span, Expr *value, uint64_t count);
/* `name` is ANY expression at parse time (the grammar is `expression ':'
 * expression`); that it is a bare field name, and that the callee is a struct,
 * are IR-stage checks. */
Expr *expr_named_arg(Arena *a, Span span, Expr *name, Expr *value);

/* ---- Statements ---- */

typedef enum {
    STMT_LET, STMT_ASSIGN, STMT_IF, STMT_WHILE, STMT_FOR,
    STMT_BREAK, STMT_CONTINUE, STMT_RETURN, STMT_EXPR, STMT_BLOCK,
} StmtKind;

struct Stmt {
    StmtKind kind;
    Span     span;
    union {
        struct { Span name_span; const char *name; Type *type; Expr *init; } let_stmt; /* type may be NULL (inferred) */
        struct { Expr *target; Expr *value; } assign_stmt;
        struct { Expr *cond; Stmt *then_branch; Stmt *else_branch; } if_stmt; /* else_branch: NULL, an if-Stmt, or a block-Stmt */
        struct { Expr *cond; Stmt *body; } while_stmt;   /* body: block-Stmt */
        struct { Stmt *init; Expr *cond; Stmt *step; Stmt *body; } for_stmt; /* init/step: let/assign/expr-Stmt; body: block-Stmt */
        struct { Expr *value; } return_stmt;             /* value: NULL = bare "return;" */
        struct { Expr *expr; } expr_stmt;
        struct { NodeList stmts; } block;                /* stmts: Stmt* */
    } as;
};

Stmt *stmt_let(Arena *a, Span span, Span name_span, const char *name, size_t name_len, Type *type, Expr *init);
Stmt *stmt_assign(Arena *a, Span span, Expr *target, Expr *value);
Stmt *stmt_if(Arena *a, Span span, Expr *cond, Stmt *then_branch, Stmt *else_branch);
Stmt *stmt_while(Arena *a, Span span, Expr *cond, Stmt *body);
Stmt *stmt_for(Arena *a, Span span, Stmt *init, Expr *cond, Stmt *step, Stmt *body);
Stmt *stmt_break(Arena *a, Span span);
Stmt *stmt_continue(Arena *a, Span span);
Stmt *stmt_return(Arena *a, Span span, Expr *value);
Stmt *stmt_expr(Arena *a, Span span, Expr *expr);
Stmt *stmt_block(Arena *a, Span span, NodeList stmts);

/* ---- Pretty-printer: the S-expression golden-test dump format ---- */

/* Writes the S-expression dump (docs/M2_PLAN.md "Golden test format for
 * ASTs") to `out`, no trailing newline. Output-only: the planned canonical
 * interchange form, but no reader exists yet (same section). */
void decl_print(FILE *out, const Decl *d);

/* Same dump format, for a standalone expression - used directly by the
 * parser's own golden tests (tests/parser/), which exercise expression
 * parsing before full declarations exist to wrap one in. `depth` is the
 * starting indent level (0 for a top-level call). */
void expr_print(FILE *out, const Expr *e, int depth);

/* Same dump format, for a standalone statement/block - used directly by
 * the parser's own golden tests, which exercise statement parsing (Step
 * 6 onward) before full declarations exist to wrap one in. */
void stmt_print(FILE *out, const Stmt *s, int depth);
