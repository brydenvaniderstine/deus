#include "ir.h"
#include <assert.h>

/* The typed-IR dump (docs/M2_PLAN.md "IR shape", Dump format). Same
 * conventions as ast.c's printer: `(tag@L:C-L:C scalars... children)`, each
 * child on its own line one indent deeper, the closing paren on the last
 * child's line. An expression's type is its last header scalar.
 *
 * Stack: ir_dump_expr/ir_dump_stmt recurse once per tree level, and the parser
 * bounds both (expression height 512, nesting 256 units). To keep the frame
 * small the recursive functions hold no formatting locals: the header (tag,
 * span, scalars) is printed by non-recursive helpers. The measured cost is in
 * ir_test.c / tools/measure_ir_stack.sh, not asserted here. */

static const char *const unop_names[] = {"!", "-", "~"}; /* UnOp order: NOT, NEG, BITNOT */
static const char *const binop_names[] = {
    "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>", "<", ">", "<=", ">=", "==", "!=", "&&", "||",
}; /* BinOp order */
_Static_assert(UNOP_BITNOT == 2, "unop_names must follow UnOp");
_Static_assert(BINOP_OR == 17, "binop_names must follow BinOp");

static void nl_indent(FILE *out, int depth) {
    fprintf(out, "\n%*s", depth * 2, "");
}

static void open_form(FILE *out, const char *tag, Span sp) {
    fprintf(out, "(%s@%d:%d-%d:%d", tag, sp.start_line, sp.start_col, sp.end_line, sp.end_col);
}

static void scalar_type(FILE *out, const IrType *t) {
    assert(t->kind != IR_T_ERROR); /* ERROR never appears in a successfully built module */
    fputc(' ', out);
    ir_type_print(out, t);
}

static void scalar_local(FILE *out, const IrLocal *l) {
    fprintf(out, " %s #%u", l->name, (unsigned)l->slot);
}

/* Header of an expression: everything up to (not including) its children. */
static void expr_header(FILE *out, const IrExpr *e) {
    switch (e->kind) {
        case IR_E_INT:
            open_form(out, "int", e->span);
            if (e->type->as.integer.is_signed) {
                fprintf(out, " %lld", (long long)(int64_t)e->as.integer.bits);
            } else {
                fprintf(out, " %llu", (unsigned long long)e->as.integer.bits);
            }
            break;
        case IR_E_BOOL:
            open_form(out, "bool", e->span);
            fputs(e->as.boolean.value ? " true" : " false", out);
            break;
        case IR_E_LOCAL:
            open_form(out, "local", e->span);
            scalar_local(out, e->as.local.local);
            break;
        case IR_E_UNARY:
            open_form(out, "unary", e->span);
            fprintf(out, " %s", unop_names[e->as.unary.op]);
            break;
        case IR_E_BINARY:
            open_form(out, "binary", e->span);
            fprintf(out, " %s", binop_names[e->as.binary.op]);
            break;
        case IR_E_CALL:
            open_form(out, "call", e->span);
            fprintf(out, " %s", e->as.call.func->id);
            break;
        case IR_E_STRUCT_NEW:
            open_form(out, "struct_new", e->span);
            break;
        case IR_E_FIELD: {
            open_form(out, "field", e->span);
            const IrStruct *sd = e->as.field.base->type->as.strukt.decl;
            fprintf(out, " %s #%u", sd->fields[e->as.field.index].name, (unsigned)e->as.field.index);
            break;
        }
        case IR_E_INDEX:
            open_form(out, "index", e->span);
            break;
        case IR_E_SLICE:
            open_form(out, "slice", e->span);
            break;
        case IR_E_CONVERT:
            open_form(out, "convert", e->span);
            break;
        case IR_E_ARRAY:
            open_form(out, "array", e->span);
            break;
        case IR_E_REPEAT:
            open_form(out, "repeat", e->span);
            fprintf(out, " %llu", (unsigned long long)e->as.repeat.count);
            break;
    }
    /* struct_new's type IS the struct, so it carries the one type scalar every
     * expression ends its header with. */
    scalar_type(out, e->type);
}

/* One child on its own line. Kept as a tiny wrapper so call sites read as a
 * list; inlining it does not change the recursion depth. */
static void child_expr(FILE *out, const IrExpr *c, int depth) {
    nl_indent(out, depth + 1);
    ir_dump_expr(out, c, depth + 1);
}

static void child_stmt(FILE *out, const IrStmt *c, int depth) {
    nl_indent(out, depth + 1);
    ir_dump_stmt(out, c, depth + 1);
}

void ir_dump_expr(FILE *out, const IrExpr *e, int depth) {
    expr_header(out, e);
    switch (e->kind) {
        case IR_E_INT:
        case IR_E_BOOL:
        case IR_E_LOCAL:
            break;
        case IR_E_UNARY:
            child_expr(out, e->as.unary.operand, depth);
            break;
        case IR_E_BINARY:
            child_expr(out, e->as.binary.lhs, depth);
            child_expr(out, e->as.binary.rhs, depth);
            break;
        case IR_E_CALL:
            for (size_t i = 0; i < e->as.call.args.len; i++) child_expr(out, e->as.call.args.items[i], depth);
            break;
        case IR_E_STRUCT_NEW:
            for (size_t i = 0; i < e->as.struct_new.values.len; i++) child_expr(out, e->as.struct_new.values.items[i], depth);
            break;
        case IR_E_FIELD:
            child_expr(out, e->as.field.base, depth);
            break;
        case IR_E_INDEX:
            child_expr(out, e->as.index.base, depth);
            child_expr(out, e->as.index.idx, depth);
            break;
        case IR_E_SLICE:
            child_expr(out, e->as.slice.base, depth);
            child_expr(out, e->as.slice.lo, depth);
            child_expr(out, e->as.slice.hi, depth);
            break;
        case IR_E_CONVERT:
            child_expr(out, e->as.convert.operand, depth);
            break;
        case IR_E_ARRAY:
            for (size_t i = 0; i < e->as.array.elems.len; i++) child_expr(out, e->as.array.elems.items[i], depth);
            break;
        case IR_E_REPEAT:
            child_expr(out, e->as.repeat.value, depth);
            break;
    }
    fputc(')', out);
}

static void stmt_header(FILE *out, const IrStmt *s) {
    switch (s->kind) {
        case IR_S_LET:
            open_form(out, "let", s->span);
            scalar_local(out, s->as.let.local);
            scalar_type(out, s->as.let.local->type);
            break;
        case IR_S_ASSIGN:   open_form(out, "assign", s->span); break;
        case IR_S_IF:       open_form(out, "if", s->span); break;
        case IR_S_WHILE:    open_form(out, "while", s->span); break;
        case IR_S_FOR:      open_form(out, "for", s->span); break;
        case IR_S_BREAK:    open_form(out, "break", s->span); break;
        case IR_S_CONTINUE: open_form(out, "continue", s->span); break;
        case IR_S_RETURN:   open_form(out, "return", s->span); break;
        case IR_S_EXPR:     open_form(out, "expr_stmt", s->span); break;
        case IR_S_BLOCK:    open_form(out, "block", s->span); break;
    }
}

void ir_dump_stmt(FILE *out, const IrStmt *s, int depth) {
    stmt_header(out, s);
    switch (s->kind) {
        case IR_S_LET:
            child_expr(out, s->as.let.init, depth);
            break;
        case IR_S_ASSIGN:
            child_expr(out, s->as.assign.place, depth);
            child_expr(out, s->as.assign.value, depth);
            break;
        case IR_S_IF:
            child_expr(out, s->as.if_stmt.cond, depth);
            child_stmt(out, s->as.if_stmt.then_branch, depth);
            if (s->as.if_stmt.else_branch) child_stmt(out, s->as.if_stmt.else_branch, depth);
            break;
        case IR_S_WHILE:
            child_expr(out, s->as.while_stmt.cond, depth);
            child_stmt(out, s->as.while_stmt.body, depth);
            break;
        case IR_S_FOR:
            child_stmt(out, s->as.for_stmt.init, depth);
            child_expr(out, s->as.for_stmt.cond, depth);
            child_stmt(out, s->as.for_stmt.step, depth);
            child_stmt(out, s->as.for_stmt.body, depth);
            break;
        case IR_S_BREAK:
        case IR_S_CONTINUE:
            break;
        case IR_S_RETURN:
            if (s->as.ret.value) child_expr(out, s->as.ret.value, depth);
            break;
        case IR_S_EXPR:
            child_expr(out, s->as.expr.expr, depth);
            break;
        case IR_S_BLOCK:
            for (size_t i = 0; i < s->as.block.stmts.len; i++) child_stmt(out, s->as.block.stmts.items[i], depth);
            break;
    }
    fputc(')', out);
}

void ir_dump_func(FILE *out, const IrFunc *f) {
    open_form(out, "fn", f->span);
    fprintf(out, " %s", f->id);
    if (f->kernel_hook) fputs(" kernel_hook", out);
    /* Parameters stay on one line, as in M2_PLAN's example. */
    nl_indent(out, 1);
    fputs("(params", out);
    for (size_t i = 0; i < f->n_params; i++) {
        const IrLocal *p = f->locals.items[i];
        assert(p->is_param && p->slot == i);
        fputc(' ', out);
        open_form(out, "param", p->span);
        scalar_local(out, p);
        scalar_type(out, p->type);
        fputc(')', out);
    }
    fputc(')', out);
    nl_indent(out, 1);
    fputs("(returns", out);
    if (f->ret->kind != IR_T_UNIT) scalar_type(out, f->ret); /* no '-> type' prints (returns) */
    fputc(')', out);
    child_stmt(out, f->body, 0);
    fputc(')', out);
}

void ir_dump_struct(FILE *out, const IrStruct *s) {
    open_form(out, "struct", s->span);
    fprintf(out, " %s size=%llu align=%u", s->name, (unsigned long long)s->size, (unsigned)s->align);
    for (size_t i = 0; i < s->n_fields; i++) {
        const IrField *f = &s->fields[i];
        nl_indent(out, 1);
        open_form(out, "field", f->span);
        fprintf(out, " %s", f->name);
        scalar_type(out, f->type);
        fprintf(out, " +%llu)", (unsigned long long)f->offset);
    }
    fputc(')', out);
}

void ir_dump_module(FILE *out, const IrModule *m) {
    for (size_t i = 0; i < m->structs.len; i++) {
        ir_dump_struct(out, m->structs.items[i]);
        fputc('\n', out);
    }
    for (size_t i = 0; i < m->funcs.len; i++) {
        ir_dump_func(out, m->funcs.items[i]);
        fputc('\n', out);
    }
}
