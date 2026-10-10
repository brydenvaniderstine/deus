#include <string.h>
#include <stdalign.h>
#include "ast.h"

/* ---------------------------------------------------------------- *
 * NodeList: generic growable array of arena-allocated pointers.
 * ---------------------------------------------------------------- */

void node_list_init(NodeList *list) {
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

void node_list_push(Arena *a, NodeList *list, void *item) {
    if (list->len == list->cap) {
        size_t new_cap = list->cap == 0 ? 4 : list->cap * 2;
        void **new_items = arena_alloc(a, new_cap * sizeof(void *), alignof(void *));
        if (list->items) {
            memcpy(new_items, list->items, list->len * sizeof(void *));
        }
        list->items = new_items;
        list->cap = new_cap;
    }
    list->items[list->len++] = item;
}

static const char *arena_strndup(Arena *a, const char *s, size_t len) {
    char *copy = arena_alloc(a, len + 1, 1);
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

/* ---------------------------------------------------------------- *
 * Constructors
 * ---------------------------------------------------------------- */

Type *type_ident(Arena *a, Span span, const char *name, size_t name_len) {
    Type *t = arena_alloc(a, sizeof(Type), alignof(Type));
    t->kind = TYPE_IDENT;
    t->span = span;
    t->as.ident.name = arena_strndup(a, name, name_len);
    return t;
}

Type *type_array(Arena *a, Span span, uint64_t size, Type *elem) {
    Type *t = arena_alloc(a, sizeof(Type), alignof(Type));
    t->kind = TYPE_ARRAY;
    t->span = span;
    t->as.array.size = size;
    t->as.array.elem = elem;
    return t;
}

Type *type_slice(Arena *a, Span span, Type *elem) {
    Type *t = arena_alloc(a, sizeof(Type), alignof(Type));
    t->kind = TYPE_SLICE;
    t->span = span;
    t->as.slice.elem = elem;
    return t;
}

Field *field_new(Arena *a, Span span, const char *name, size_t name_len, Type *type) {
    Field *f = arena_alloc(a, sizeof(Field), alignof(Field));
    f->span = span;
    f->name = arena_strndup(a, name, name_len);
    f->type = type;
    return f;
}

Decl *decl_struct(Arena *a, Span span, const char *name, size_t name_len, NodeList fields) {
    Decl *d = arena_alloc(a, sizeof(Decl), alignof(Decl));
    d->kind = DECL_STRUCT;
    d->span = span;
    d->as.struct_decl.name = arena_strndup(a, name, name_len);
    d->as.struct_decl.fields = fields;
    return d;
}

Decl *decl_function(Arena *a, Span span, NodeList attributes, const char *name, size_t name_len,
                     NodeList params, Type *return_type, Stmt *body) {
    Decl *d = arena_alloc(a, sizeof(Decl), alignof(Decl));
    d->kind = DECL_FUNCTION;
    d->span = span;
    d->as.function_decl.attributes = attributes;
    d->as.function_decl.name = arena_strndup(a, name, name_len);
    d->as.function_decl.params = params;
    d->as.function_decl.return_type = return_type;
    d->as.function_decl.body = body;
    return d;
}

Expr *expr_number(Arena *a, Span span, uint64_t value) {
    Expr *e = arena_alloc(a, sizeof(Expr), alignof(Expr));
    e->kind = EXPR_NUMBER;
    e->span = span;
    e->parenthesized = false;
    e->as.number.value = value;
    return e;
}

Expr *expr_bool(Arena *a, Span span, bool value) {
    Expr *e = arena_alloc(a, sizeof(Expr), alignof(Expr));
    e->kind = EXPR_BOOL;
    e->span = span;
    e->parenthesized = false;
    e->as.boolean.value = value;
    return e;
}

Expr *expr_ident(Arena *a, Span span, const char *name, size_t name_len) {
    Expr *e = arena_alloc(a, sizeof(Expr), alignof(Expr));
    e->kind = EXPR_IDENT;
    e->span = span;
    e->parenthesized = false;
    e->as.ident.name = arena_strndup(a, name, name_len);
    return e;
}

Expr *expr_unary(Arena *a, Span span, UnOp op, Expr *operand) {
    Expr *e = arena_alloc(a, sizeof(Expr), alignof(Expr));
    e->kind = EXPR_UNARY;
    e->span = span;
    e->parenthesized = false;
    e->as.unary.op = op;
    e->as.unary.operand = operand;
    return e;
}

Expr *expr_binary(Arena *a, Span span, BinOp op, Expr *lhs, Expr *rhs) {
    Expr *e = arena_alloc(a, sizeof(Expr), alignof(Expr));
    e->kind = EXPR_BINARY;
    e->span = span;
    e->parenthesized = false;
    e->as.binary.op = op;
    e->as.binary.lhs = lhs;
    e->as.binary.rhs = rhs;
    return e;
}

Expr *expr_call(Arena *a, Span span, const char *name, size_t name_len, NodeList args) {
    Expr *e = arena_alloc(a, sizeof(Expr), alignof(Expr));
    e->kind = EXPR_CALL;
    e->span = span;
    e->parenthesized = false;
    e->as.call.name = arena_strndup(a, name, name_len);
    e->as.call.args = args;
    return e;
}

Expr *expr_convert(Arena *a, Span span, const char *target, size_t target_len, Expr *operand) {
    Expr *e = arena_alloc(a, sizeof(Expr), alignof(Expr));
    e->kind = EXPR_CONVERT;
    e->span = span;
    e->parenthesized = false;
    e->as.convert.target = arena_strndup(a, target, target_len);
    e->as.convert.operand = operand;
    return e;
}

Expr *expr_array(Arena *a, Span span, NodeList elems) {
    Expr *e = arena_alloc(a, sizeof(Expr), alignof(Expr));
    e->kind = EXPR_ARRAY;
    e->span = span;
    e->parenthesized = false;
    e->as.array.elems = elems;
    return e;
}

Expr *expr_array_repeat(Arena *a, Span span, Expr *value, uint64_t count) {
    Expr *e = arena_alloc(a, sizeof(Expr), alignof(Expr));
    e->kind = EXPR_ARRAY_REPEAT;
    e->span = span;
    e->parenthesized = false;
    e->as.array_repeat.value = value;
    e->as.array_repeat.count = count;
    return e;
}

Expr *expr_named_arg(Arena *a, Span span, Expr *name, Expr *value) {
    Expr *e = arena_alloc(a, sizeof(Expr), alignof(Expr));
    e->kind = EXPR_NAMED_ARG;
    e->span = span;
    e->parenthesized = false;
    e->as.named_arg.name = name;
    e->as.named_arg.value = value;
    return e;
}

Expr *expr_index(Arena *a, Span span, Expr *base, Expr *index) {
    Expr *e = arena_alloc(a, sizeof(Expr), alignof(Expr));
    e->kind = EXPR_INDEX;
    e->span = span;
    e->parenthesized = false;
    e->as.index.base = base;
    e->as.index.index = index;
    return e;
}

Expr *expr_slice(Arena *a, Span span, Expr *base, Expr *lo, Expr *hi) {
    Expr *e = arena_alloc(a, sizeof(Expr), alignof(Expr));
    e->kind = EXPR_SLICE;
    e->span = span;
    e->parenthesized = false;
    e->as.slice.base = base;
    e->as.slice.lo = lo;
    e->as.slice.hi = hi;
    return e;
}

Expr *expr_field_access(Arena *a, Span span, Expr *base, const char *name, size_t name_len) {
    Expr *e = arena_alloc(a, sizeof(Expr), alignof(Expr));
    e->kind = EXPR_FIELD_ACCESS;
    e->span = span;
    e->parenthesized = false;
    e->as.field_access.base = base;
    e->as.field_access.name = arena_strndup(a, name, name_len);
    return e;
}

Stmt *stmt_let(Arena *a, Span span, const char *name, size_t name_len, Type *type, Expr *init) {
    Stmt *s = arena_alloc(a, sizeof(Stmt), alignof(Stmt));
    s->kind = STMT_LET;
    s->span = span;
    s->as.let_stmt.name = arena_strndup(a, name, name_len);
    s->as.let_stmt.type = type;
    s->as.let_stmt.init = init;
    return s;
}

Stmt *stmt_assign(Arena *a, Span span, Expr *target, Expr *value) {
    Stmt *s = arena_alloc(a, sizeof(Stmt), alignof(Stmt));
    s->kind = STMT_ASSIGN;
    s->span = span;
    s->as.assign_stmt.target = target;
    s->as.assign_stmt.value = value;
    return s;
}

Stmt *stmt_if(Arena *a, Span span, Expr *cond, Stmt *then_branch, Stmt *else_branch) {
    Stmt *s = arena_alloc(a, sizeof(Stmt), alignof(Stmt));
    s->kind = STMT_IF;
    s->span = span;
    s->as.if_stmt.cond = cond;
    s->as.if_stmt.then_branch = then_branch;
    s->as.if_stmt.else_branch = else_branch;
    return s;
}

Stmt *stmt_while(Arena *a, Span span, Expr *cond, Stmt *body) {
    Stmt *s = arena_alloc(a, sizeof(Stmt), alignof(Stmt));
    s->kind = STMT_WHILE;
    s->span = span;
    s->as.while_stmt.cond = cond;
    s->as.while_stmt.body = body;
    return s;
}

Stmt *stmt_for(Arena *a, Span span, Stmt *init, Expr *cond, Stmt *step, Stmt *body) {
    Stmt *s = arena_alloc(a, sizeof(Stmt), alignof(Stmt));
    s->kind = STMT_FOR;
    s->span = span;
    s->as.for_stmt.init = init;
    s->as.for_stmt.cond = cond;
    s->as.for_stmt.step = step;
    s->as.for_stmt.body = body;
    return s;
}

Stmt *stmt_break(Arena *a, Span span) {
    Stmt *s = arena_alloc(a, sizeof(Stmt), alignof(Stmt));
    s->kind = STMT_BREAK;
    s->span = span;
    return s;
}

Stmt *stmt_continue(Arena *a, Span span) {
    Stmt *s = arena_alloc(a, sizeof(Stmt), alignof(Stmt));
    s->kind = STMT_CONTINUE;
    s->span = span;
    return s;
}

Stmt *stmt_return(Arena *a, Span span, Expr *value) {
    Stmt *s = arena_alloc(a, sizeof(Stmt), alignof(Stmt));
    s->kind = STMT_RETURN;
    s->span = span;
    s->as.return_stmt.value = value;
    return s;
}

Stmt *stmt_expr(Arena *a, Span span, Expr *expr) {
    Stmt *s = arena_alloc(a, sizeof(Stmt), alignof(Stmt));
    s->kind = STMT_EXPR;
    s->span = span;
    s->as.expr_stmt.expr = expr;
    return s;
}

Stmt *stmt_block(Arena *a, Span span, NodeList stmts) {
    Stmt *s = arena_alloc(a, sizeof(Stmt), alignof(Stmt));
    s->kind = STMT_BLOCK;
    s->span = span;
    s->as.block.stmts = stmts;
    return s;
}

/* ---------------------------------------------------------------- *
 * Pretty-printer: the S-expression golden-test dump format.
 *
 * Lisp-style layout: a node with only simple/inline children (an
 * identifier, a bare type name, ...) prints fully on one line. A node
 * with one or more "complex" children prints each on its own line,
 * indented two spaces deeper, and closes with a bare ")" appended
 * directly after the last child - closing parens for every ancestor
 * with no more siblings left cascade onto that same trailing line.
 * This is exactly what docs/M2_PLAN.md's own example shows, reproduced
 * byte-for-byte by tests/ast/basic_function (see ast_test.c).
 * ---------------------------------------------------------------- */

static void nl_indent(FILE *out, int depth) {
    fprintf(out, "\n%*s", depth * 2, "");
}

static void type_print_inline(FILE *out, const Type *t) {
    switch (t->kind) {
        case TYPE_IDENT:
            fprintf(out, "%s", t->as.ident.name);
            break;
        case TYPE_ARRAY:
            fprintf(out, "[%llu]", (unsigned long long)t->as.array.size);
            type_print_inline(out, t->as.array.elem);
            break;
        case TYPE_SLICE:
            fprintf(out, "[]");
            type_print_inline(out, t->as.slice.elem);
            break;
    }
}

static const char *unop_text(UnOp op) {
    switch (op) {
        case UNOP_NOT:    return "!";
        case UNOP_NEG:    return "-";
        case UNOP_BITNOT: return "~";
    }
    return "?";
}

static const char *binop_text(BinOp op) {
    switch (op) {
        case BINOP_ADD: return "+";
        case BINOP_SUB: return "-";
        case BINOP_MUL: return "*";
        case BINOP_DIV: return "/";
        case BINOP_MOD: return "%";
        case BINOP_BITAND: return "&";
        case BINOP_BITOR:  return "|";
        case BINOP_BITXOR: return "^";
        case BINOP_SHL: return "<<";
        case BINOP_SHR: return ">>";
        case BINOP_LT: return "<";
        case BINOP_GT: return ">";
        case BINOP_LE: return "<=";
        case BINOP_GE: return ">=";
        case BINOP_EQ: return "==";
        case BINOP_NE: return "!=";
        case BINOP_AND: return "&&";
        case BINOP_OR:  return "||";
    }
    return "?";
}

static void print_span(FILE *out, const char *kind, Span sp) {
    fprintf(out, "(%s@%d:%d-%d:%d", kind, sp.start_line, sp.start_col, sp.end_line, sp.end_col);
}

void expr_print(FILE *out, const Expr *e, int depth) {
    switch (e->kind) {
        case EXPR_NUMBER:
            print_span(out, "number", e->span);
            fprintf(out, " %llu)", (unsigned long long)e->as.number.value);
            return;
        case EXPR_BOOL:
            print_span(out, "bool", e->span);
            fprintf(out, " %s)", e->as.boolean.value ? "true" : "false");
            return;
        case EXPR_IDENT:
            print_span(out, "ident", e->span);
            fprintf(out, " %s)", e->as.ident.name);
            return;
        case EXPR_UNARY:
            print_span(out, "unary", e->span);
            fprintf(out, " %s", unop_text(e->as.unary.op));
            nl_indent(out, depth + 1);
            expr_print(out, e->as.unary.operand, depth + 1);
            fprintf(out, ")");
            return;
        case EXPR_BINARY:
            print_span(out, "binary", e->span);
            fprintf(out, " %s", binop_text(e->as.binary.op));
            nl_indent(out, depth + 1);
            expr_print(out, e->as.binary.lhs, depth + 1);
            nl_indent(out, depth + 1);
            expr_print(out, e->as.binary.rhs, depth + 1);
            fprintf(out, ")");
            return;
        case EXPR_CALL:
            print_span(out, "call", e->span);
            fprintf(out, " %s", e->as.call.name);
            for (size_t i = 0; i < e->as.call.args.len; i++) {
                nl_indent(out, depth + 1);
                expr_print(out, (const Expr *)e->as.call.args.items[i], depth + 1);
            }
            fprintf(out, ")");
            return;
        case EXPR_CONVERT:
            print_span(out, "convert", e->span);
            fprintf(out, " %s", e->as.convert.target);
            nl_indent(out, depth + 1);
            expr_print(out, e->as.convert.operand, depth + 1);
            fprintf(out, ")");
            return;
        case EXPR_ARRAY:
            print_span(out, "array", e->span);
            for (size_t i = 0; i < e->as.array.elems.len; i++) {
                nl_indent(out, depth + 1);
                expr_print(out, (const Expr *)e->as.array.elems.items[i], depth + 1);
            }
            fprintf(out, ")");
            return;
        case EXPR_ARRAY_REPEAT:
            print_span(out, "array_repeat", e->span);
            fprintf(out, " %llu", (unsigned long long)e->as.array_repeat.count);
            nl_indent(out, depth + 1);
            expr_print(out, e->as.array_repeat.value, depth + 1);
            fprintf(out, ")");
            return;
        case EXPR_NAMED_ARG:
            print_span(out, "named_arg", e->span);
            nl_indent(out, depth + 1);
            expr_print(out, e->as.named_arg.name, depth + 1);
            nl_indent(out, depth + 1);
            expr_print(out, e->as.named_arg.value, depth + 1);
            fprintf(out, ")");
            return;
        case EXPR_INDEX:
            print_span(out, "index", e->span);
            nl_indent(out, depth + 1);
            expr_print(out, e->as.index.base, depth + 1);
            nl_indent(out, depth + 1);
            expr_print(out, e->as.index.index, depth + 1);
            fprintf(out, ")");
            return;
        case EXPR_SLICE:
            print_span(out, "slice", e->span);
            nl_indent(out, depth + 1);
            expr_print(out, e->as.slice.base, depth + 1);
            nl_indent(out, depth + 1);
            expr_print(out, e->as.slice.lo, depth + 1);
            nl_indent(out, depth + 1);
            expr_print(out, e->as.slice.hi, depth + 1);
            fprintf(out, ")");
            return;
        case EXPR_FIELD_ACCESS:
            print_span(out, "field_access", e->span);
            nl_indent(out, depth + 1);
            expr_print(out, e->as.field_access.base, depth + 1);
            fprintf(out, " %s)", e->as.field_access.name);
            return;
    }
}

void stmt_print(FILE *out, const Stmt *s, int depth) {
    switch (s->kind) {
        case STMT_LET:
            print_span(out, "let_stmt", s->span);
            if (s->as.let_stmt.type) {
                fprintf(out, " %s:", s->as.let_stmt.name);
                type_print_inline(out, s->as.let_stmt.type);
            } else {
                fprintf(out, " %s", s->as.let_stmt.name);
            }
            nl_indent(out, depth + 1);
            expr_print(out, s->as.let_stmt.init, depth + 1);
            fprintf(out, ")");
            return;
        case STMT_ASSIGN:
            print_span(out, "assign_stmt", s->span);
            nl_indent(out, depth + 1);
            expr_print(out, s->as.assign_stmt.target, depth + 1);
            nl_indent(out, depth + 1);
            expr_print(out, s->as.assign_stmt.value, depth + 1);
            fprintf(out, ")");
            return;
        case STMT_IF:
            print_span(out, "if_stmt", s->span);
            nl_indent(out, depth + 1);
            expr_print(out, s->as.if_stmt.cond, depth + 1);
            nl_indent(out, depth + 1);
            stmt_print(out, s->as.if_stmt.then_branch, depth + 1);
            if (s->as.if_stmt.else_branch) {
                nl_indent(out, depth + 1);
                stmt_print(out, s->as.if_stmt.else_branch, depth + 1);
            }
            fprintf(out, ")");
            return;
        case STMT_WHILE:
            print_span(out, "while_stmt", s->span);
            nl_indent(out, depth + 1);
            expr_print(out, s->as.while_stmt.cond, depth + 1);
            nl_indent(out, depth + 1);
            stmt_print(out, s->as.while_stmt.body, depth + 1);
            fprintf(out, ")");
            return;
        case STMT_FOR:
            print_span(out, "for_stmt", s->span);
            nl_indent(out, depth + 1);
            stmt_print(out, s->as.for_stmt.init, depth + 1);
            nl_indent(out, depth + 1);
            expr_print(out, s->as.for_stmt.cond, depth + 1);
            nl_indent(out, depth + 1);
            stmt_print(out, s->as.for_stmt.step, depth + 1);
            nl_indent(out, depth + 1);
            stmt_print(out, s->as.for_stmt.body, depth + 1);
            fprintf(out, ")");
            return;
        case STMT_BREAK:
            print_span(out, "break_stmt", s->span);
            fprintf(out, ")");
            return;
        case STMT_CONTINUE:
            print_span(out, "continue_stmt", s->span);
            fprintf(out, ")");
            return;
        case STMT_RETURN:
            print_span(out, "return_stmt", s->span);
            if (s->as.return_stmt.value) {
                nl_indent(out, depth + 1);
                expr_print(out, s->as.return_stmt.value, depth + 1);
            }
            fprintf(out, ")");
            return;
        case STMT_EXPR:
            print_span(out, "expr_stmt", s->span);
            nl_indent(out, depth + 1);
            expr_print(out, s->as.expr_stmt.expr, depth + 1);
            fprintf(out, ")");
            return;
        case STMT_BLOCK:
            print_span(out, "block", s->span);
            for (size_t i = 0; i < s->as.block.stmts.len; i++) {
                nl_indent(out, depth + 1);
                stmt_print(out, (const Stmt *)s->as.block.stmts.items[i], depth + 1);
            }
            fprintf(out, ")");
            return;
    }
}

static void field_print(FILE *out, const char *kind, const Field *f, int depth) {
    (void)depth;
    print_span(out, kind, f->span);
    fprintf(out, " %s ", f->name);
    type_print_inline(out, f->type);
    fprintf(out, ")");
}

void decl_print(FILE *out, const Decl *d) {
    switch (d->kind) {
        case DECL_STRUCT:
            print_span(out, "struct_decl", d->span);
            fprintf(out, " %s", d->as.struct_decl.name);
            if (d->as.struct_decl.fields.len > 0) {
                nl_indent(out, 1);
                fprintf(out, "(fields");
                for (size_t i = 0; i < d->as.struct_decl.fields.len; i++) {
                    nl_indent(out, 2);
                    field_print(out, "field", (const Field *)d->as.struct_decl.fields.items[i], 2);
                }
                fprintf(out, ")");
            }
            fprintf(out, ")");
            return;
        case DECL_FUNCTION:
            print_span(out, "function_decl", d->span);
            fprintf(out, " %s", d->as.function_decl.name);
            if (d->as.function_decl.attributes.len > 0) {
                nl_indent(out, 1);
                fprintf(out, "(attributes");
                for (size_t i = 0; i < d->as.function_decl.attributes.len; i++) {
                    fprintf(out, " %s", (const char *)d->as.function_decl.attributes.items[i]);
                }
                fprintf(out, ")");
            }
            nl_indent(out, 1);
            fprintf(out, "(params");
            for (size_t i = 0; i < d->as.function_decl.params.len; i++) {
                nl_indent(out, 2);
                field_print(out, "param", (const Field *)d->as.function_decl.params.items[i], 2);
            }
            fprintf(out, ")");
            nl_indent(out, 1);
            if (d->as.function_decl.return_type) {
                fprintf(out, "(return_type ");
                type_print_inline(out, d->as.function_decl.return_type);
                fprintf(out, ")");
            } else {
                /* Absent '-> type' = returns nothing. `unit` is a reserved word
                 * that cannot be written (E0109), so the word never appears:
                 * a dump containing `(return_type unit)` is not a valid
                 * interchange form (docs/M2_PLAN.md golden-format section). */
                fprintf(out, "(return_type)");
            }
            nl_indent(out, 1);
            stmt_print(out, d->as.function_decl.body, 1);
            fprintf(out, ")");
            return;
    }
}
