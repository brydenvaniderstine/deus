#include "ir_internal.h"
#include <stdlib.h>
#include <string.h>

/* Rename fix-its (typed-IR step 14, docs/M2_PLAN.md "Naming conventions"). A misnamed declaration (E0280 /
 * E0281) gets a Rename that records every resolved use while the bodies are built; after the last body the fix
 * is decided and attached to the diagnostic: the declaration's name and every use replaced by the converted
 * name, as one fix with 1 + uses edits. Only misnamed declarations are tracked, so a well-named program records
 * nothing. Nothing here depends on hash order or pointers: renames are processed in the order they were created
 * (declaration order, then body order) and the edits are sorted by source position. */

static Rename **table_for(IrState *st, Rename ***slot, size_t n) {
    if (!*slot) {
        *slot = arena_alloc(st->arena, (n ? n : 1) * sizeof(Rename *), alignof(Rename *));
        for (size_t i = 0; i < n; i++) (*slot)[i] = NULL;
    }
    return *slot;
}

Rename *ir_check_naming(IrState *st, Ctx *cx, RenameKind kind, size_t scope, bool pascal, const char *name, Span at) {
    if (pascal ? ir_is_pascal_case(name) : ir_is_snake_case(name)) return NULL;
    cx->naming_errors++;
    Rename *r = arena_alloc(st->arena, sizeof(Rename), alignof(Rename));
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->pascal = pascal;
    r->scope = scope;
    r->name = name;
    r->decl_span = at;
    r->diag = st->diags.len;
    if (cx->n_renames == cx->cap_renames) {
        size_t cap = cx->cap_renames ? cx->cap_renames * 2 : 8;
        Rename **list = arena_alloc(st->arena, cap * sizeof(Rename *), alignof(Rename *));
        if (cx->n_renames) memcpy(list, cx->renames, cx->n_renames * sizeof(Rename *));
        cx->renames = list; /* installed only after the allocation succeeded */
        cx->cap_renames = cap;
    }
    cx->renames[cx->n_renames++] = r;
    ir_error(st, pascal ? diag_ir_not_pascal_case(st->arena, at, name) : diag_ir_not_snake_case(st->arena, at, name));
    return r;
}

void ir_note_use(IrState *st, Rename *r, Span at) {
    if (!r) return;
    UseList *u = &r->uses;
    if (u->len == u->cap) {
        size_t cap = u->cap ? u->cap * 2 : 8;
        Span *spans = arena_alloc(st->arena, cap * sizeof(Span), alignof(Span));
        if (u->len) memcpy(spans, u->spans, u->len * sizeof(Span));
        u->spans = spans;
        u->cap = cap;
    }
    u->spans[u->len++] = at;
}

void body_set_rename(BodyCtx *bc, size_t slot, Rename *r) {
    if (slot >= bc->renames_cap) {
        size_t cap = bc->renames_cap ? bc->renames_cap : 8;
        while (cap <= slot) cap *= 2;
        Rename **t = arena_alloc(bc->st->arena, cap * sizeof(Rename *), alignof(Rename *));
        for (size_t i = 0; i < cap; i++) t[i] = i < bc->renames_cap ? bc->renames[i] : NULL;
        bc->renames = t;
        bc->renames_cap = cap;
    }
    bc->renames[slot] = r;
    r->slot = (uint32_t)slot;
}

/* The tables the declaration pass and the checker fill. Allocated on first use. */
Rename **ir_struct_rename_table(IrState *st, Ctx *cx) { return table_for(st, &cx->struct_renames, cx->cap_decl_items); }
Rename **ir_fn_rename_table(IrState *st, Ctx *cx) { return table_for(st, &cx->fn_renames, cx->cap_decl_items); }

static int cmp_span(const void *pa, const void *pb) {
    const Span *a = pa, *b = pb;
    if (a->start_line != b->start_line) return a->start_line < b->start_line ? -1 : 1;
    if (a->start_col != b->start_col) return a->start_col < b->start_col ? -1 : 1;
    if (a->end_line != b->end_line) return a->end_line < b->end_line ? -1 : 1;
    if (a->end_col != b->end_col) return a->end_col < b->end_col ? -1 : 1;
    return 0;
}

/* Can one of two locals of a function be seen from where the other is declared? A local is visible from its declaration to the end of
 * its scope: a later slot below scope_end is declared inside that region. Two locals that cannot see each other (sibling blocks, two
 * sibling loops, a block that closed before the other local) may share a name, so they may share a fix target. */
static bool locals_see_each_other(const IrFunc *f, uint32_t a, uint32_t b) {
    const IrLocal *la = f->locals.items[a], *lb = f->locals.items[b];
    return (a < b && b < la->scope_end) || (b < a && a < lb->scope_end);
}

/* Is `target` already a name in the scope of `r` as written, or the target of an earlier fix in that scope? */
static bool collides(const Ctx *cx, const Rename *r, const char *target, size_t upto, const char *const *granted) {
    switch (r->kind) {
        case REN_TOP:
            if (symtab_lookup(&cx->top, target)) return true;
            break;
        case REN_FIELD: {
            const IrStruct *s = &cx->irs[r->scope];
            for (size_t i = 0; i < s->n_fields; i++) if (strcmp(s->fields[i].name, target) == 0) return true;
            break;
        }
        case REN_LOCAL: {
            const IrFunc *f = cx->fns[r->scope];
            for (size_t i = 0; i < f->locals.len; i++) {
                if (strcmp(((const IrLocal *)f->locals.items[i])->name, target) == 0 && locals_see_each_other(f, r->slot, (uint32_t)i)) return true;
            }
            break;
        }
    }
    for (size_t j = 0; j < upto; j++) {
        const Rename *o = cx->renames[j];
        if (granted[j] && o->kind == r->kind && o->scope == r->scope && o->pascal == r->pascal && strcmp(granted[j], target) == 0 &&
            (r->kind != REN_LOCAL || locals_see_each_other(cx->fns[r->scope], r->slot, o->slot))) return true;
        /* one namespace at the top level: a struct's target and a function's target must differ too */
        if (granted[j] && r->kind == REN_TOP && o->kind == REN_TOP && strcmp(granted[j], target) == 0) return true;
    }
    return false;
}

void ir_finalize_renames(IrState *st, Ctx *cx) {
    if (cx->n_renames == 0) return;
    const char **granted = arena_alloc(st->arena, cx->n_renames * sizeof(const char *), alignof(const char *));
    for (size_t i = 0; i < cx->n_renames; i++) {
        Rename *r = cx->renames[i];
        granted[i] = NULL;
        const char *target = r->pascal ? ir_to_pascal_case(st->arena, r->name) : ir_to_snake_case(st->arena, r->name);
        if (!target || ir_is_reserved_name(target) || collides(cx, r, target, i, granted)) continue;
        granted[i] = target;

        size_t n = 1 + r->uses.len;
        Span *spans = arena_alloc(st->arena, n * sizeof(Span), alignof(Span));
        spans[0] = r->decl_span;
        for (size_t k = 0; k < r->uses.len; k++) spans[k + 1] = r->uses.spans[k];
        qsort(spans, n, sizeof(Span), cmp_span);
        size_t m = 0; /* a use seen twice would be one edit applied twice */
        for (size_t k = 0; k < n; k++) {
            if (m && cmp_span(&spans[m - 1], &spans[k]) == 0) continue;
            spans[m++] = spans[k];
        }
        DiagFix *fix = diag_fix_new(st->arena, (int)m);
        fix->title = arena_sprintf(st->arena, "rename '%s' to '%s'", r->name, target);
        for (size_t k = 0; k < m; k++) {
            fix->edits[k].span = spans[k];
            fix->edits[k].text = target;
        }
        fix->n_edits = (int)m;
        st->diags.items[r->diag].fix = fix;
    }
}

/* ---- applying fixes to source text (the test harness and a future `manna fix`) ---- */

typedef struct { Span span; const char *text; } Edit;

static int cmp_edit(const void *pa, const void *pb) {
    return cmp_span(&((const Edit *)pa)->span, &((const Edit *)pb)->span);
}

char *ir_apply_fixes(const char *src, size_t len, const DiagList *diags) {
    size_t n = 0;
    for (size_t i = 0; i < diags->len; i++) if (diags->items[i].fix) n += (size_t)diags->items[i].fix->n_edits;
    Edit *edits = malloc((n ? n : 1) * sizeof(Edit));
    size_t k = 0;
    for (size_t i = 0; i < diags->len; i++) {
        const DiagFix *f = diags->items[i].fix;
        for (int j = 0; f && j < f->n_edits; j++) edits[k++] = (Edit){f->edits[j].span, f->edits[j].text};
    }
    qsort(edits, n, sizeof(Edit), cmp_edit);
    /* byte offset of each line start */
    size_t lines = 1;
    for (size_t i = 0; i < len; i++) lines += src[i] == '\n';
    size_t *line_start = malloc((lines + 1) * sizeof(size_t));
    line_start[0] = 0;
    size_t li = 1;
    for (size_t i = 0; i < len; i++) if (src[i] == '\n') line_start[li++] = i + 1;
    line_start[li] = len;
    size_t cap = len + 1;
    for (size_t i = 0; i < n; i++) cap += strlen(edits[i].text);
    char *out = malloc(cap + 1);
    size_t o = 0, pos = 0;
    for (size_t i = 0; i < n; i++) {
        size_t a = line_start[edits[i].span.start_line - 1] + (size_t)edits[i].span.start_col - 1;
        size_t b = line_start[edits[i].span.end_line - 1] + (size_t)edits[i].span.end_col - 1;
        if (a < pos) continue; /* overlapping edits are never produced; skip rather than corrupt */
        memcpy(out + o, src + pos, a - pos);
        o += a - pos;
        size_t tl = strlen(edits[i].text);
        memcpy(out + o, edits[i].text, tl);
        o += tl;
        pos = b;
    }
    memcpy(out + o, src + pos, len - pos);
    o += len - pos;
    out[o] = '\0';
    free(edits);
    free(line_start);
    return out;
}
