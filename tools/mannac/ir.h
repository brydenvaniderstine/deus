#pragma once
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "arena.h"
#include "ast.h"
#include "diag.h"

/* Typed IR (docs/M2_PLAN.md "Typed IR (stage 3): decided rules", "IR shape").
 * A separate structured tree with its OWN arena and hard cap; the AST is never
 * mutated and must outlive the IR (names are pointers into the AST arena's
 * strings). Working checklist: docs/TYPED_IR.md.
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
    jmp_buf      fatal_target; /* ir_build owns the setjmp; E0201/E0203 land here */
    IrType       prim[IR_N_PRIM];
    IrTypeTable  composites;
    Span         progress;     /* the declaration being built: where E0201 reports */
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
 * reset; what it holds stays, bounded by its cap. */
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
