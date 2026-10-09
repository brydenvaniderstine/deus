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
    const char *name;
    Type       *type;
} Field; /* also used for function parameters - same shape */

typedef Field Param;

Field *field_new(Arena *a, Span span, const char *name, size_t name_len, Type *type);

typedef enum { DECL_STRUCT, DECL_FUNCTION } DeclKind;

struct Decl {
    DeclKind kind;
    Span     span;
    union {
        struct {
            const char *name;
            NodeList    fields;   /* Field* */
        } struct_decl;
        struct {
            NodeList    attributes; /* const char* - e.g. "kernel_hook" */
            const char *name;
            NodeList    params;     /* Param* */
            Type       *return_type; /* NULL = unit (no '-> type' present) */
            Stmt       *body;       /* always a STMT_BLOCK */
        } function_decl;
    } as;
};

Decl *decl_struct(Arena *a, Span span, const char *name, size_t name_len, NodeList fields);
Decl *decl_function(Arena *a, Span span, NodeList attributes, const char *name, size_t name_len,
                     NodeList params, Type *return_type, Stmt *body);

/* ---- Expressions ---- */

typedef enum {
    EXPR_NUMBER, EXPR_BOOL, EXPR_IDENT,
    EXPR_UNARY, EXPR_BINARY,
    EXPR_CALL, EXPR_INDEX, EXPR_SLICE, EXPR_FIELD_ACCESS,
} ExprKind;

typedef enum { UNOP_NOT, UNOP_NEG, UNOP_BITNOT } UnOp;

typedef enum {
    BINOP_ADD, BINOP_SUB, BINOP_MUL, BINOP_DIV, BINOP_MOD,
    BINOP_BITAND, BINOP_BITOR, BINOP_BITXOR,
    BINOP_SHL, BINOP_SHR,
    BINOP_LT, BINOP_GT, BINOP_LE, BINOP_GE, BINOP_EQ, BINOP_NE,
    BINOP_AND, BINOP_OR,
} BinOp;

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
    union {
        struct { uint64_t value; } number;
        struct { bool value; } boolean;
        struct { const char *name; } ident;
        struct { UnOp op; Expr *operand; } unary;
        struct { BinOp op; Expr *lhs; Expr *rhs; } binary;
        struct { const char *name; NodeList args; } call;      /* args: Expr* */
        struct { Expr *base; Expr *index; } index;
        struct { Expr *base; Expr *lo; Expr *hi; } slice;
        struct { Expr *base; const char *name; } field_access;
    } as;
};

Expr *expr_number(Arena *a, Span span, uint64_t value);
Expr *expr_bool(Arena *a, Span span, bool value);
Expr *expr_ident(Arena *a, Span span, const char *name, size_t name_len);
Expr *expr_unary(Arena *a, Span span, UnOp op, Expr *operand);
Expr *expr_binary(Arena *a, Span span, BinOp op, Expr *lhs, Expr *rhs);
Expr *expr_call(Arena *a, Span span, const char *name, size_t name_len, NodeList args);
Expr *expr_index(Arena *a, Span span, Expr *base, Expr *index);
Expr *expr_slice(Arena *a, Span span, Expr *base, Expr *lo, Expr *hi);
Expr *expr_field_access(Arena *a, Span span, Expr *base, const char *name, size_t name_len);

/* ---- Statements ---- */

typedef enum {
    STMT_LET, STMT_ASSIGN, STMT_IF, STMT_WHILE, STMT_FOR,
    STMT_BREAK, STMT_CONTINUE, STMT_RETURN, STMT_EXPR, STMT_BLOCK,
} StmtKind;

struct Stmt {
    StmtKind kind;
    Span     span;
    union {
        struct { const char *name; Type *type; Expr *init; } let_stmt; /* type may be NULL (inferred) */
        struct { Expr *target; Expr *value; } assign_stmt;
        struct { Expr *cond; Stmt *then_branch; Stmt *else_branch; } if_stmt; /* else_branch: NULL, an if-Stmt, or a block-Stmt */
        struct { Expr *cond; Stmt *body; } while_stmt;   /* body: block-Stmt */
        struct { Stmt *init; Expr *cond; Stmt *step; Stmt *body; } for_stmt; /* init/step: let/assign/expr-Stmt; body: block-Stmt */
        struct { Expr *value; } return_stmt;             /* value: NULL = bare "return;" */
        struct { Expr *expr; } expr_stmt;
        struct { NodeList stmts; } block;                /* stmts: Stmt* */
    } as;
};

Stmt *stmt_let(Arena *a, Span span, const char *name, size_t name_len, Type *type, Expr *init);
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
