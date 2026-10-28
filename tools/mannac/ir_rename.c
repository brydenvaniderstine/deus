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
    if (kind != REN_LOCAL) cx->n_nonlocal_renames++;
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

const char *ir_rename_target(IrState *st, Rename *r) {
    if (!r->target_done) {
        r->target = r->pascal ? ir_to_pascal_case(st->arena, r->name) : ir_to_snake_case(st->arena, r->name);
        r->target_done = true;
    }
    return r->target;
}

static uint64_t unres_key(int ns, size_t scope, const char *name) {
    uint64_t h = 1469598103934665603ULL ^ (uint64_t)ns;
    h = (h ^ (uint64_t)scope) * 1099511628211ULL;
    for (const unsigned char *c = (const unsigned char *)name; *c; c++) h = (h ^ *c) * 1099511628211ULL;
    return h;
}

/* Only a rename of a struct, function or field consults this list (collides), and those are all created before any body is checked, so
 * while none exists nothing is recorded. A name repeated any number of times is stored once. */
void ir_note_unresolved(IrState *st, Ctx *cx, int ns, size_t scope, const char *name) {
    if (cx->n_nonlocal_renames == 0) return;
    if ((cx->n_unresolved + 1) * 2 > cx->unres_hash_cap) { /* grow the set (and rebuild it from the list) */
        size_t cap = cx->unres_hash_cap ? cx->unres_hash_cap * 2 : 16;
        uint32_t *t = arena_alloc(st->arena, cap * sizeof(uint32_t), alignof(uint32_t));
        memset(t, 0, cap * sizeof(uint32_t));
        for (size_t i = 0; i < cx->n_unresolved; i++) {
            size_t j = unres_key(cx->unresolved[i].ns, cx->unresolved[i].scope, cx->unresolved[i].name) & (cap - 1);
            while (t[j]) j = (j + 1) & (cap - 1);
            t[j] = (uint32_t)(i + 1);
        }
        cx->unres_hash = t;
        cx->unres_hash_cap = cap;
    }
    size_t mask = cx->unres_hash_cap - 1, j = unres_key(ns, scope, name) & mask;
    for (; cx->unres_hash[j]; j = (j + 1) & mask) {
        const Unresolved *u = &cx->unresolved[cx->unres_hash[j] - 1];
        if (u->ns == ns && u->scope == scope && strcmp(u->name, name) == 0) return;
    }
    if (cx->n_unresolved == cx->cap_unresolved) {
        size_t cap = cx->cap_unresolved ? cx->cap_unresolved * 2 : 8;
        Unresolved *list = arena_alloc(st->arena, cap * sizeof(Unresolved), alignof(Unresolved));
        if (cx->n_unresolved) memcpy(list, cx->unresolved, cx->n_unresolved * sizeof(Unresolved));
        cx->unresolved = list;
        cx->cap_unresolved = cap;
    }
    cx->unresolved[cx->n_unresolved] = (Unresolved){name, ns, scope};
    cx->unres_hash[j] = (uint32_t)(++cx->n_unresolved);
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

/* Every Rename of a function's locals lives in cx->renames, and each E0280 counts toward the 20-error cap, so there are at most 20 of
 * them in all: this loops over those, not over the function's slots, and compares converted names computed once (ir_rename_target). */
void ir_note_free_name(BodyCtx *bc, const char *name, Rename *top) {
    IrState *st = bc->st;
    const char *top_target = NULL; /* where `top` is going: it must not land on a visible local, or on the target of a visible local */
    if (top) {
        top_target = ir_rename_target(st, top);
        if (top_target && body_lookup(bc, top_target)) top->blocked = true;
    }
    const Ctx *cx = bc->decls;
    for (size_t i = 0; i < cx->n_renames; i++) {
        Rename *r = cx->renames[i];
        if (r->kind != REN_LOCAL || r->blocked || r->scope != bc->fn_index) continue;
        const IrLocal *seen = body_lookup(bc, r->name); /* visible here? */
        if (!seen || seen->slot != r->slot) continue;
        const char *t = ir_rename_target(st, r);
        if (t && (strcmp(t, name) == 0 || (top_target && strcmp(t, top_target) == 0))) r->blocked = true;
    }
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

/* Does the set of unresolved names (a hash set, ir_note_unresolved) hold this one? */
static bool unresolved_has(const Ctx *cx, int ns, size_t scope, const char *name) {
    if (cx->unres_hash_cap == 0) return false;
    size_t mask = cx->unres_hash_cap - 1, j = unres_key(ns, scope, name) & mask;
    for (; cx->unres_hash[j]; j = (j + 1) & mask) {
        const Unresolved *u = &cx->unresolved[cx->unres_hash[j] - 1];
        if (u->ns == ns && u->scope == scope && strcmp(u->name, name) == 0) return true;
    }
    return false;
}

/* The locals of ONE function by name, built the first time a rename of that function is decided: `heads[hash]` is the last local whose name
 * hashes there and `next[slot]` the previous one, so "which locals are called `target`?" visits only the locals in one bucket instead of
 * all of them (re-audit 3: the cost of a rename must not grow with the size of the function). */
typedef struct {
    size_t     fn;        /* the function the index is of (SIZE_MAX: none yet) */
    size_t     nbuckets;  /* power of two */
    uint32_t  *heads;     /* slot + 1, 0 = empty */
    uint32_t  *next;      /* per slot: the previous slot in the same bucket, + 1 */
} LocalIndex;

static uint64_t local_name_hash(const char *n) {
    uint64_t h = 1469598103934665603ULL;
    for (const unsigned char *c = (const unsigned char *)n; *c; c++) h = (h ^ *c) * 1099511628211ULL;
    return h;
}

static void local_index_build(IrState *st, const Ctx *cx, LocalIndex *li, size_t fn) {
    const IrFunc *f = cx->fns[fn];
    size_t nb = 16;
    while (nb < f->locals.len * 2) nb *= 2;
    li->fn = fn;
    li->nbuckets = nb;
    li->heads = arena_alloc(st->arena, nb * sizeof(uint32_t), alignof(uint32_t));
    li->next = arena_alloc(st->arena, (f->locals.len ? f->locals.len : 1) * sizeof(uint32_t), alignof(uint32_t));
    memset(li->heads, 0, nb * sizeof(uint32_t));
    for (size_t i = 0; i < f->locals.len; i++) {
        size_t b = local_name_hash(((const IrLocal *)f->locals.items[i])->name) & (nb - 1);
        li->next[i] = li->heads[b];
        li->heads[b] = (uint32_t)(i + 1);
    }
}

/* Is `target` already a name in the scope of `r` as written, or the target of an earlier fix in that scope? */
static bool collides(IrState *st, const Ctx *cx, LocalIndex *li, const Rename *r, const char *target, size_t upto, const char *const *granted) {
    /* a name that resolved to nothing would start to resolve to this declaration: a real collision. (A name that resolved to nothing and
     * spells the declaration AS WRITTEN is not one: it is a use that is renamed with the declaration, ir_note_use at the place that
     * could not resolve it. Re-audit 3, finding 6.) */
    int ns = r->kind == REN_TOP ? NS_TOP : NS_FIELD;
    if (r->kind != REN_LOCAL && unresolved_has(cx, ns, r->kind == REN_TOP ? 0 : r->scope, target)) return true;
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
            if (li->fn != r->scope) local_index_build(st, cx, li, r->scope);
            for (uint32_t i = li->heads[local_name_hash(target) & (li->nbuckets - 1)]; i; i = li->next[i - 1]) {
                if (strcmp(((const IrLocal *)f->locals.items[i - 1])->name, target) == 0 && locals_see_each_other(f, r->slot, i - 1)) return true;
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
    LocalIndex li = {SIZE_MAX, 0, NULL, NULL};
    for (size_t i = 0; i < cx->n_renames; i++) {
        Rename *r = cx->renames[i];
        granted[i] = NULL;
        const char *target = ir_rename_target(st, r);
        if (!target || r->blocked || ir_is_reserved_name(target) || collides(st, cx, &li, r, target, i, granted)) continue;
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

static bool ident_byte(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

void ir_edit_padding(const char *src, size_t len, size_t start, size_t end, const char *text, bool *space_before, bool *space_after) {
    size_t tl = strlen(text);
    if (start != end) { /* a replacement keeps the boundaries of the token it replaces: its neighbours are not identifier characters */
        *space_before = *space_after = false;
        return;
    }
    *space_before = tl && start > 0 && start <= len && ident_byte(text[0]) && ident_byte(src[start - 1]);
    *space_after = tl && end < len && ident_byte(text[tl - 1]) && ident_byte(src[end]);
}

char *ir_apply_fixes_map(const char *src, size_t len, const DiagList *diags, size_t *out_len, FixSpan **spans_out, size_t *n_spans_out) {
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
    for (size_t i = 0; i < n; i++) cap += strlen(edits[i].text) + 2; /* + a separating space on each side (ir_edit_padding) */
    char *out = malloc(cap + 1);
    FixSpan *applied = spans_out ? malloc((n ? n : 1) * sizeof(FixSpan)) : NULL;
    size_t n_applied = 0;
    size_t o = 0, pos = 0;
    for (size_t i = 0; i < n; i++) {
        size_t a = line_start[edits[i].span.start_line - 1] + (size_t)edits[i].span.start_col - 1;
        size_t b = line_start[edits[i].span.end_line - 1] + (size_t)edits[i].span.end_col - 1;
        if (a < pos) continue; /* overlapping edits are never produced; skip rather than corrupt */
        memcpy(out + o, src + pos, a - pos);
        o += a - pos;
        bool before, after;
        ir_edit_padding(src, len, a, b, edits[i].text, &before, &after);
        size_t tl = strlen(edits[i].text);
        size_t o_start = o;
        if (before) out[o++] = ' ';
        memcpy(out + o, edits[i].text, tl);
        o += tl;
        if (after) out[o++] = ' ';
        if (applied) applied[n_applied++] = (FixSpan){a, b, o - o_start};
        pos = b;
    }
    memcpy(out + o, src + pos, len - pos);
    o += len - pos;
    out[o] = '\0';
    free(edits);
    free(line_start);
    if (out_len) *out_len = o;
    if (spans_out) { *spans_out = applied; *n_spans_out = n_applied; }
    return out;
}

char *ir_apply_fixes_n(const char *src, size_t len, const DiagList *diags, size_t *out_len) {
    return ir_apply_fixes_map(src, len, diags, out_len, NULL, NULL);
}

char *ir_apply_fixes(const char *src, size_t len, const DiagList *diags) {
    return ir_apply_fixes_n(src, len, diags, NULL);
}
