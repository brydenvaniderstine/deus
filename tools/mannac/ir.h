#pragma once
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "arena.h"
#include "ast.h"
#include "diag.h"

/* Typed IR (docs/M2_PLAN.md "Typed IR (stage 3): decided rules", "IR shape").
 * A separate structured tree with its OWN arena and hard cap; the AST is never
 * mutated and must outlive the IR (names are pointers into the AST arena's
 * strings). Working checklist: docs/history/TYPED_IR.md.
 *
 * This header is built up across typed-IR step 3: 3a (this part) is types,
 * interning and IrState; 3b adds the node vocabulary and the dumper; 3c adds
 * ir_build and the parse-diagnostics gate. */

#define IR_MAX_ERRORS 20          /* the parser's MAX_ERRORS precedent (E0203) */
#define IR_ARENA_CAP  (16u << 20) /* same as the AST arena; E0201 when exceeded */

/* ---- Types ---- */

typedef struct IrType   IrType;
typedef struct IrStruct IrStruct;

typedef enum {
    IR_T_INT, IR_T_BOOL, IR_T_UNIT, IR_T_ARRAY, IR_T_SLICE, IR_T_STRUCT,
    IR_T_ERROR, /* the poison type: compatible with everything, never reported
                 * again, never present in a successfully built module */
} IrTypeKind;

/* Interned and canonical per IrState: two IrType pointers from the same state
 * are equal iff the types are equal, so type comparison is pointer comparison.
 * UNIT exists internally (a function with no '-> type' returns it) although it
 * is unwritable in source. */
struct IrType {
    IrTypeKind kind;
    union {
        struct { uint8_t bits; bool is_signed; } integer;      /* bits: 8, 16, 32, 64 */
        struct { uint64_t len; const IrType *elem; } array;     /* [len]elem */
        struct { const IrType *elem; } slice;                   /* []elem */
        struct { const IrStruct *decl; } strukt;
    } as;
};

typedef struct {
    const char   *name;
    Span          span;
    const IrType *type;
    uint64_t      offset; /* byte offset in the struct (layout: step 4) */
} IrField;

struct IrStruct {
    const char *name;
    Span        span;
    IrField    *fields;
    size_t      n_fields;
    uint64_t    size;
    uint32_t    align;
};

/* ---- State ---- */

/* The eleven non-composite types live INSIDE IrState, not in the arena: their
 * addresses are fixed for the state's lifetime, so identity survives any arena
 * state (an overflow longjmp, reuse, a future arena reset). */
#define IR_N_PRIM 11 /* u8 i8 u16 i16 u32 i32 u64 i64 bool unit error */

/* Open-addressing set of the composite (array/slice/struct) types. Looked up
 * only, NEVER iterated: no output may depend on its layout or on pointer
 * values (principle 12). */
typedef struct {
    const IrType **slots; /* arena-allocated, cap entries, NULL = empty */
    size_t         cap;
    size_t         count;
} IrTypeTable;

typedef struct {
    Arena       *arena;
    DiagList     diags;
    int          error_count;
    int          muted;       /* > 0: ir_error records nothing (a subtree checked only for its call edges and name uses); always 0 outside one call of ir_body.c:check_muted or ir_expr.c:group_fits */
    size_t       decl_diags_end; /* diagnostics before this index came from the declaration pass; set when the bodies are reached (the whole list if they never were) */
    bool         bodies_reached; /* the build got past the declaration pass (a naming error alone does not stop it) */
    size_t       muted_hits;  /* how many diagnostics ir_error dropped because muted > 0 (a dry check compares it before and after) */
    jmp_buf      fatal_target; /* ir_build owns the setjmp; E0201/E0203 land here */
    IrType       prim[IR_N_PRIM];
    IrTypeTable  composites;
    Span         progress;     /* the declaration being built: where E0201 reports */
    const char  *unimplemented; /* set with IR_UNIMPLEMENTED: the construct step 3 cannot build yet */
    size_t       expr_visits;   /* expression nodes visited by the checker this build (tests prove the checker is linear) */
} IrState;

/* An IrState used across a setjmp/longjmp must have static (or otherwise
 * non-indeterminate) storage: C11 7.13.2.1, the lesson of the parser's F5. */
void ir_state_init(IrState *st, size_t arena_max_size);
void ir_state_destroy(IrState *st);

/* Resets every per-build field: error_count, progress, the overflow hook
 * (re-armed), the primitive types (rebuilt in place, same addresses) and the
 * composite table (emptied: STRUCT entries point into an abandoned module).
 * diags is deliberately NOT cleared - it is the accumulated result list, owned
 * by the caller (the parser's reset makes the same choice). The arena is not
 * reset; what it holds stays, bounded by its cap - so a state that ended in
 * E0201 stays exhausted: a build on it reports E0201 again (the hook is
 * re-armed; it is never silent) until the state is destroyed. */
void ir_state_reset_for_build(IrState *st);

/* Records a diagnostic. On the IR_MAX_ERRORS'th also records E0203 and
 * longjmps to st->fatal_target (never returns then). */
void ir_error(IrState *st, Diagnostic d);

/* ---- Type constructors / queries ---- */

const IrType *ir_type_int(IrState *st, int bits, bool is_signed); /* bits: 8/16/32/64 */
const IrType *ir_type_bool(IrState *st);
const IrType *ir_type_unit(IrState *st);
const IrType *ir_type_error(IrState *st);
const IrType *ir_type_array(IrState *st, uint64_t len, const IrType *elem);
const IrType *ir_type_slice(IrState *st, const IrType *elem);
const IrType *ir_type_struct(IrState *st, const IrStruct *decl);

/* Canonical text: u8..u64, i8..i64, bool, unit, [4]u8, []u8, Point, <error>.
 * Primitives return static strings; composites are arena-allocated. This text
 * is part of the dump format AND of the type-mismatch messages. */
const char *ir_type_text(IrState *st, const IrType *t);

/* Writes the canonical type text (same as ir_type_text) straight to `out`:
 * no state, no allocation, so the dumper can never longjmp. */
void ir_type_print(FILE *out, const IrType *t);

/* ---- Nodes (the vocabulary of M2_PLAN "IR shape") ---- */

typedef struct IrExpr IrExpr;
typedef struct IrStmt IrStmt;
typedef struct IrFunc IrFunc;

typedef struct {
    uint32_t       slot;     /* declaration order; parameters first */
    const char    *name;     /* points into the AST arena */
    const IrType  *type;
    Span           span;
    bool           is_param;
    uint32_t       fix_diag;  /* internal, not dumped: 1 + the index of the E0261 diagnostic whose fix would give this local its type (it is ERROR now); 0: none. See ir_expr.c "a local that waits for its uses" */
    const IrType  *fix_type;  /* ... the type that fix would give it */
    uint32_t       scope_end; /* internal, not dumped: the number of locals when its scope closed (UINT32_MAX while open and for parameters); a local declared later in the same function with a slot below this can see it */
} IrLocal;

typedef enum {
    IR_E_INT, IR_E_BOOL, IR_E_LOCAL, IR_E_UNARY, IR_E_BINARY, IR_E_CALL,
    IR_E_STRUCT_NEW, IR_E_FIELD, IR_E_INDEX, IR_E_SLICE, IR_E_CONVERT,
    IR_E_ARRAY, IR_E_REPEAT, IR_E_LEN,
} IrExprKind;

/* Every expression has a type (never ERROR in a built module) and the span of
 * the AST node it came from. Whether an operation traps (arithmetic, shifts,
 * division) or short-circuits (&&, ||) is a function of its operator and type,
 * so it is neither stored nor printed. */
struct IrExpr {
    IrExprKind    kind;
    Span          span;
    const IrType *type;
    union {
        struct { uint64_t bits; } integer;   /* two's complement, sign-extended to 64 bits for signed types */
        struct { bool value; } boolean;
        struct { const IrLocal *local; } local;
        struct { UnOp op; IrExpr *operand; } unary;
        struct { BinOp op; IrExpr *lhs; IrExpr *rhs; } binary;
        struct { const IrFunc *func; NodeList args; } call;      /* args: IrExpr* */
        struct { NodeList values; } struct_new;                  /* values: IrExpr*, declaration order */
        struct { IrExpr *base; uint32_t index; } field;          /* index into the base struct's fields */
        struct { IrExpr *base; IrExpr *idx; } index;
        struct { IrExpr *base; IrExpr *lo; IrExpr *hi; } slice;
        struct { IrExpr *operand; } convert;                     /* target type == expr type */
        struct { NodeList elems; } array;                        /* elems: IrExpr* */
        struct { IrExpr *value; uint64_t count; } repeat;       /* count == the array type's length */
        struct { IrExpr *operand; } len;                         /* len(x), type u64; the operand is kept so it is evaluated exactly once */
    } as;
};

typedef enum {
    IR_S_LET, IR_S_ASSIGN, IR_S_IF, IR_S_WHILE, IR_S_FOR,
    IR_S_BREAK, IR_S_CONTINUE, IR_S_RETURN, IR_S_EXPR, IR_S_BLOCK,
} IrStmtKind;

struct IrStmt {
    IrStmtKind kind;
    Span       span;
    union {
        struct { const IrLocal *local; IrExpr *init; } let;
        struct { IrExpr *place; IrExpr *value; } assign;
        struct { IrExpr *cond; IrStmt *then_branch; IrStmt *else_branch; } if_stmt; /* else: NULL, an IF or a BLOCK */
        struct { IrExpr *cond; IrStmt *body; } while_stmt;
        struct { IrStmt *init; IrExpr *cond; IrStmt *step; IrStmt *body; } for_stmt; /* `for` stays a `for` */
        struct { IrExpr *value; } ret;                           /* NULL = bare return */
        struct { IrExpr *expr; } expr;
        struct { NodeList stmts; } block;                        /* IrStmt* */
    } as;
};

struct IrFunc {
    Span          span;
    const char   *name;
    const char   *id;          /* "fn:name" (docs/M2_PLAN.md S7 item 5) */
    bool          kernel_hook;
    NodeList      locals;      /* IrLocal*, every slot in slot order, parameters first */
    size_t        n_params;
    const IrType *ret;         /* unit when the source has no '-> type' */
    IrStmt       *body;        /* an IR_S_BLOCK */
};

typedef struct {
    NodeList structs;          /* IrStruct*, declaration order */
    NodeList funcs;            /* IrFunc*, declaration order */
} IrModule;

/* Constructors (arena-allocated like ast.c's; they may longjmp to
 * st->fatal_target on arena exhaustion, E0201). */
IrLocal *ir_local_new(IrState *st, uint32_t slot, const char *name, const IrType *type, Span span, bool is_param);
IrExpr *ir_expr_int(IrState *st, Span span, const IrType *type, uint64_t bits);
IrExpr *ir_expr_bool(IrState *st, Span span, bool value);
IrExpr *ir_expr_local(IrState *st, Span span, const IrLocal *local);
IrExpr *ir_expr_unary(IrState *st, Span span, const IrType *type, UnOp op, IrExpr *operand);
IrExpr *ir_expr_binary(IrState *st, Span span, const IrType *type, BinOp op, IrExpr *lhs, IrExpr *rhs);
IrExpr *ir_expr_call(IrState *st, Span span, const IrType *type, const IrFunc *func, NodeList args);
IrExpr *ir_expr_struct_new(IrState *st, Span span, const IrType *type, NodeList values);
IrExpr *ir_expr_field(IrState *st, Span span, const IrType *type, IrExpr *base, uint32_t index);
IrExpr *ir_expr_index(IrState *st, Span span, const IrType *type, IrExpr *base, IrExpr *idx);
IrExpr *ir_expr_slice(IrState *st, Span span, const IrType *type, IrExpr *base, IrExpr *lo, IrExpr *hi);
IrExpr *ir_expr_convert(IrState *st, Span span, const IrType *type, IrExpr *operand);
IrExpr *ir_expr_array(IrState *st, Span span, const IrType *type, NodeList elems);
IrExpr *ir_expr_repeat(IrState *st, Span span, const IrType *type, IrExpr *value, uint64_t count);
IrExpr *ir_expr_len(IrState *st, Span span, const IrType *type, IrExpr *operand);
IrStmt *ir_stmt_let(IrState *st, Span span, const IrLocal *local, IrExpr *init);
IrStmt *ir_stmt_assign(IrState *st, Span span, IrExpr *place, IrExpr *value);
IrStmt *ir_stmt_if(IrState *st, Span span, IrExpr *cond, IrStmt *then_branch, IrStmt *else_branch);
IrStmt *ir_stmt_while(IrState *st, Span span, IrExpr *cond, IrStmt *body);
IrStmt *ir_stmt_for(IrState *st, Span span, IrStmt *init, IrExpr *cond, IrStmt *step, IrStmt *body);
IrStmt *ir_stmt_break(IrState *st, Span span);
IrStmt *ir_stmt_continue(IrState *st, Span span);
IrStmt *ir_stmt_return(IrState *st, Span span, IrExpr *value);
IrStmt *ir_stmt_expr(IrState *st, Span span, IrExpr *expr);
IrStmt *ir_stmt_block(IrState *st, Span span, NodeList stmts);
IrFunc *ir_func_new(IrState *st, Span span, const char *name, bool kernel_hook, NodeList locals,
                    size_t n_params, const IrType *ret, IrStmt *body);

/* True if the AST expression respects the parser's E0111 bound. The IR stage
 * ASSERTS the bound, it enforces none of its own (docs/history/TYPED_IR.md Q14): the
 * builder will `assert(ir_ast_height_ok(e))` at every expression root. O(1):
 * it reads Expr.height, the constructors having computed it. */
bool ir_ast_height_ok(const Expr *e);

/* ---- Dump (docs/M2_PLAN.md "IR shape", Dump format) ----
 * Output-only S-expressions, the AST dump's conventions: `(tag@L:C-L:C
 * scalars... children)`, two-space indent per level, an expression's type is
 * its LAST header scalar, locals print `name #slot`, nothing printed depends
 * on a pointer, hash order or time. No trailing newline. The dumpers are
 * recursive; their stack cost is bounded by the parser's depth and height
 * bounds and measured (tools/measure_ir_stack.sh), see ir_test.c. */
void ir_dump_expr(FILE *out, const IrExpr *e, int depth);
void ir_dump_stmt(FILE *out, const IrStmt *s, int depth);
void ir_dump_func(FILE *out, const IrFunc *f);
void ir_dump_struct(FILE *out, const IrStruct *s);
/* Structs first (declaration order), then functions (declaration order), one
 * form per line, each followed by '\n'. An empty module prints nothing. */
void ir_dump_module(FILE *out, const IrModule *m);

/* ---- Building ---- */

typedef enum {
    IR_BUILT,         /* *out is a complete module */
    IR_SKIPPED,       /* the parse reported diagnostics: the stage did not run (no diagnostics added) */
    IR_ERRORS,        /* E02xx diagnostics recorded in st->diags; *out is NULL */
    IR_UNIMPLEMENTED, /* TEMPORARY: the file uses a construct the skeleton cannot build yet
                       * (st->unimplemented names it). Not a diagnostic and never a golden:
                       * the harness exits nonzero. Deleted when the last construct lands. */
} IrStatus;

/* AST -> typed IR. `parse_diags` is the parse's DiagList: the stage refuses to
 * run on a file with ANY parse diagnostic (docs/M2_PLAN.md "Input contract",
 * Gate) - a quality policy against cascades from a tree with holes, not a
 * safety dependency; the builder must still not crash on any tree the parser
 * can produce. `items` is parser_parse_file's output. *out is NULL unless the
 * result is IR_BUILT. Owns its setjmp: E0201 and E0203 unwind to it. */
IrStatus ir_build(IrState *st, const DiagList *parse_diags, const NodeList *items, IrModule **out);


/* Applies every fix attached to `diags` to the source text: all edits sorted by position and applied together
 * (the edits of one fix, and of different fixes, never overlap). Returns a malloc'd NUL-terminated copy
 * (typed-IR step 14, ir_rename.c). */
char *ir_apply_fixes(const char *src, size_t len, const DiagList *diags);

/* The self-check (step 20f; docs/error_codes.md "Fix-it rules"). A fix-it the compiler offers must make the program better and change nothing else.
 * `orig` is a state whose ir_build just returned IR_ERRORS for `src`. Applies every fix, rebuilds the fixed source with a fresh state and compares:
 *   (a) every diagnostic that offered a fix is gone;
 *   (b) every other diagnostic is unchanged (same line, code and message; a name quoted in a message may differ only if an applied rename fix
 *       renamed it: the rename titles are the only source of the mapping);
 *   (c) no diagnostic appears that was not there.
 * Exception, only when the original build STOPPED (a declaration error, E0203 or E0201): the diagnostics of the declaration pass must still
 * satisfy (a)-(c), and the body pass, which did not run (or not to the end) before, may report new diagnostics.
 * Returns NULL if the fixes hold, else a malloc'd explanation (the caller prints "internal error: a fix-it did not re-check" with it and
 * exits non-zero, or aborts: a failure is a bug in the compiler, never filtered or hidden). Also NULL when `orig` offers no fix. */
char *ir_fix_selfcheck(const char *src, size_t len, const IrState *orig);

/* The one rule every function that applies a DiagEdit to source text must follow (docs/error_codes.md, fix-it rules): an edit never
 * joins two tokens. `start`..`end` are the byte offsets the edit replaces (equal for an insert: only an insert is padded, a replacement keeps
 * the boundaries of the token it replaces) and `text` is what it puts there; if
 * the text begins with an identifier character and the byte before `start` is one, the applier writes a space first (`return-a` with
 * `u32(` inserted before `-a` is `return u32(-a)`, not `returnu32(-a)`); likewise after the text if it ends with one and the byte at
 * `end` is one. (Typed-IR step 19, re-audit 3 finding 2; ir_apply_fixes is the only applier today.) */
void ir_edit_padding(const char *src, size_t len, size_t start, size_t end, const char *text, bool *space_before, bool *space_after);

/* ---- Expressions (ir_expr.c, typed-IR step 6) ---- */

/* Stops the build with IR_UNIMPLEMENTED (st->unimplemented names the construct): unwinds
 * to ir_build, so it may be called from any depth. Never returns. */
_Noreturn void ir_unimplemented(IrState *st, const char *what);

/* Source spelling of an operator (`+`, `<<`, `&&`, `-`, `!`, ...): shared by the dump and the
 * diagnostics. */
const char *ir_binop_text(BinOp op);
const char *ir_unop_text(UnOp op);
