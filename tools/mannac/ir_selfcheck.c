#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"
#include "lexer.h"
#include "parser.h"
#include "parser_state.h"

/* The fix-it self-check (typed-IR step 20f). See ir.h for the invariant. A diagnostic is identified by (line, code, message). A fix CAN change lines (the E0215
 * fix deletes a whole attribute, a newline with it) and columns, so every position of an original diagnostic - its line, and any `at L:C` its message quotes
 * (only E0210 does: diag_ir_duplicate_decl) - is mapped through the edits that were applied (ir_apply_fixes_map) before it is compared (step 22A, round-4 finding 1).
 * Messages quote names ('myFn'); a rename fix may change
 * the quoted spelling in a message that mentions the name (the approved shape: a function used as a type stays E0220 with the new name). So a
 * re-check message matches an original one if it is equal except for names that an APPLIED rename renamed (the fix titles `rename 'old' to 'new'`
 * are the only source of the mapping), word by word with either spelling (names_match): nothing else about a message may differ. */

typedef struct {
    char *old_name, *new_name;
} Rename;

typedef struct {
    int   line;
    char *code;
    char *msg;
    bool  used;
} Key;

static char *dup_n(const char *s, size_t n) {
    char *r = malloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static char *dup(const char *s) { return dup_n(s, strlen(s)); }

static bool word_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

/* Does the re-check message `re` match the original `orig`? They must be equal except that a word (an identifier) of `orig` that an applied rename
 * renamed may appear in `re` with either spelling, independently word by word: a rename is scoped (a local's to its function, a field's to its
 * struct), so the same spelling elsewhere may stay while another occurrence in the same message changes. Messages mention names quoted
 * ('myFn') and unquoted (a type in "expected T, found U"); the old names are all non-conforming ones (a capital in a snake_case name, a doubled
 * underscore, ...), so no English word of a message can be one. */
static bool names_match(const char *orig, const char *re, const Rename *rn, size_t n_rn) {
    const char *p = orig, *q = re;
    while (*p && *q) {
        if (word_char(*p) && word_char(*q)) {
            size_t pl = 0, ql = 0;
            while (word_char(p[pl])) pl++;
            while (word_char(q[ql])) ql++;
            bool same = pl == ql && memcmp(p, q, pl) == 0;
            for (size_t i = 0; i < n_rn && !same; i++) {
                same = strlen(rn[i].old_name) == pl && memcmp(rn[i].old_name, p, pl) == 0 && strlen(rn[i].new_name) == ql && memcmp(rn[i].new_name, q, ql) == 0;
            }
            if (!same) return false;
            p += pl;
            q += ql;
        } else {
            if (*p != *q) return false;
            p++;
            q++;
        }
    }
    return *p == '\0' && *q == '\0';
}

/* ---- positions: the old source's (line, column) -> the fixed source's, through the applied edits ---- */

typedef struct {
    const char     *src, *fixed;
    size_t          len, flen;
    const FixSpan  *sp;
    size_t          n_sp;
} PosMap;

static size_t offset_of(const char *t, size_t len, int line, int col) {
    size_t off = 0;
    for (int l = 1; l < line && off < len; off++) if (t[off] == '\n') l++;
    return off + (size_t)col - 1;
}

/* an offset inside a replaced or deleted range maps to where the replacement starts; an insertion at the offset itself shifts it */
static size_t map_offset(const PosMap *m, size_t off) {
    long delta = 0;
    for (size_t i = 0; i < m->n_sp; i++) {
        if (m->sp[i].b <= off) delta += (long)m->sp[i].new_len - (long)(m->sp[i].b - m->sp[i].a);
        else if (m->sp[i].a <= off) return (size_t)((long)m->sp[i].a + delta);
        else break;
    }
    return (size_t)((long)off + delta);
}

static void map_pos(const PosMap *m, int line, int col, int *nline, int *ncol) {
    size_t off = map_offset(m, offset_of(m->src, m->len, line, col)), ls = 0;
    int l = 1;
    for (size_t i = 0; i < off && i < m->flen; i++) if (m->fixed[i] == '\n') { l++; ls = i + 1; }
    *nline = l;
    *ncol = (int)(off - ls) + 1;
}

/* the message with every `at L:C` mapped (a copy) */
static char *map_message(const PosMap *m, const char *msg) {
    size_t n = strlen(msg);
    size_t cap = 4 * n + 32; /* "at 1:1" (6 bytes) becomes at most "at 2147483647:2147483647" (24) */
    char *out = malloc(cap), *o = out;
    for (const char *p = msg; *p;) {
        int l, c, used = 0;
        if (strncmp(p, "at ", 3) == 0 && sscanf(p + 3, "%d:%d%n", &l, &c, &used) == 2 && used > 0) {
            int nl, nc;
            map_pos(m, l, c, &nl, &nc);
            o += snprintf(o, cap - (size_t)(o - out), "at %d:%d", nl, nc);
            p += 3 + used;
        } else {
            *o++ = *p++;
        }
    }
    *o = '\0';
    return out;
}

static void key_free(Key *k, size_t n) {
    for (size_t i = 0; i < n; i++) {
        free(k[i].code);
        free(k[i].msg);
    }
    free(k);
}

/* Match `want` (a multiset) against `have`; every `want` must be found. Returns the index of the first `want` not found, or -1. Marks `have` used. */
static long take_all(Key *want, size_t nw, Key *have, size_t nh, const Rename *rn, size_t n_rn) {
    for (size_t i = 0; i < nw; i++) {
        bool found = false;
        for (size_t j = 0; j < nh && !found; j++) {
            if (!have[j].used && have[j].line == want[i].line && strcmp(have[j].code, want[i].code) == 0 &&
                names_match(want[i].msg, have[j].msg, rn, n_rn)) {
                have[j].used = true;
                found = true;
            }
        }
        if (!found) return (long)i;
    }
    return -1;
}

static char *explain(const char *what, const Key *k) {
    char buf[512];
    snprintf(buf, sizeof buf, "%s: line %d %s %s", what, k->line, k->code, k->msg);
    return dup(buf);
}

static bool has_code(const DiagList *d, const char *code) {
    for (size_t i = 0; i < d->len; i++) if (strcmp(d->items[i].code, code) == 0) return true;
    return false;
}

/* Parses `text` into `ps` (initialized here). The setjmp is in this small function so ir_fix_selfcheck keeps no variable across it (gcc -Wclobbered). */
static void parse_fixed(ParserState *ps, NodeList *items, const char *text, size_t len) {
    node_list_init(items);
    parser_state_init(ps, 16 * 1024 * 1024);
    if (setjmp(ps->fatal_target) == 0) {
        Lexer lx;
        lexer_init(&lx, text, len);
        (void)parser_parse_file(ps, &lx, items);
    }
}

char *ir_fix_selfcheck(const char *src, size_t len, const IrState *orig) {
    const DiagList *od = &orig->diags;
    size_t nfix = 0;
    for (size_t i = 0; i < od->len; i++) nfix += od->items[i].fix != NULL;
    if (nfix == 0) return NULL;

    /* the renames the applied fixes make */
    Rename *rn = calloc(nfix, sizeof *rn);
    size_t n_rn = 0;
    for (size_t i = 0; i < od->len; i++) {
        const DiagFix *f = od->items[i].fix;
        if (!f || !f->title || strncmp(f->title, "rename '", 8) != 0) continue;
        const char *a = f->title + 8, *b = strstr(a, "' to '");
        if (!b) continue;
        const char *c = b + 6, *e = strrchr(c, '\'');
        if (!e) continue;
        rn[n_rn].old_name = dup_n(a, (size_t)(b - a));
        rn[n_rn].new_name = dup_n(c, (size_t)(e - c));
        n_rn++;
    }

    /* A build stopped by E0201 or E0203 is unfinished: it must have cleared every fix (ir_error, on_arena_overflow). A fix that survives is a bug. */
    if (has_code(od, "E0203") || has_code(od, "E0201")) {
        for (size_t i = 0; i < od->len; i++) {
            if (od->items[i].fix) {
                char buf[256];
                snprintf(buf, sizeof buf, "a fix survives a build stopped by %s (an unfinished build offers none): line %d %s %s",
                         has_code(od, "E0203") ? "E0203" : "E0201", od->items[i].span.start_line, od->items[i].code, od->items[i].message);
                for (size_t k = 0; k < n_rn; k++) { free(rn[k].old_name); free(rn[k].new_name); }
                free(rn);
                return dup(buf);
            }
        }
    }
    size_t decl1 = orig->bodies_reached ? orig->decl_diags_end : od->len;
    bool stopped = !orig->bodies_reached || has_code(od, "E0203") || has_code(od, "E0201");

    /* the fixed source, rebuilt with a fresh state */
    size_t fixed_len = 0, n_sp = 0;
    FixSpan *sp = NULL;
    char *fixed = ir_apply_fixes_map(src, len, od, &fixed_len, &sp, &n_sp);
    PosMap pm = {src, fixed, len, fixed_len, sp, n_sp};
    char *why = NULL;
    static ParserState ps;   /* large: kept off the stack */
    NodeList items;
    parse_fixed(&ps, &items, fixed, fixed_len);
    IrState st;
    ir_state_init(&st, IR_ARENA_CAP);
    IrModule *m = NULL;
    IrStatus status = ir_build(&st, &ps.diags, &items, &m);
    if (status == IR_SKIPPED) {
        char buf[512];
        const Diagnostic *d = &ps.diags.items[0];
        snprintf(buf, sizeof buf, "the fixed source does not parse: %d:%d %s %s", d->span.start_line, d->span.start_col, d->code, d->message);
        why = dup(buf);
    } else if (status == IR_UNIMPLEMENTED) {
        why = dup("the fixed source reached IR_UNIMPLEMENTED");
    } else {
        const DiagList *nd = &st.diags;
        size_t decl2 = st.bodies_reached ? st.decl_diags_end : nd->len;
        /* the original diagnostics that did NOT offer a fix, split by pass; names mapped through the applied renames */
        Key *d1 = calloc(od->len + 1, sizeof(Key)), *b1 = calloc(od->len + 1, sizeof(Key));
        size_t n_d1 = 0, n_b1 = 0;
        for (size_t i = 0; i < od->len; i++) {
            if (od->items[i].fix) continue;
            int nl, nc;
            map_pos(&pm, od->items[i].span.start_line, od->items[i].span.start_col, &nl, &nc);
            Key k = {nl, dup(od->items[i].code), map_message(&pm, od->items[i].message), false};
            if (i < decl1) d1[n_d1++] = k;
            else b1[n_b1++] = k;
        }
        Key *r_d = calloc(nd->len + 1, sizeof(Key)), *r_b = calloc(nd->len + 1, sizeof(Key));
        size_t n_rd = 0, n_rb = 0;
        for (size_t i = 0; i < nd->len; i++) {
            Key k = {nd->items[i].span.start_line, dup(nd->items[i].code), dup(nd->items[i].message), false};
            if (i < decl2) r_d[n_rd++] = k;
            else r_b[n_rb++] = k;
        }
        /* the declaration pass: unchanged (a)-(c), stopped or not */
        long miss = take_all(d1, n_d1, r_d, n_rd, rn, n_rn);
        if (miss >= 0) why = explain("(a)/(b) a declaration-pass diagnostic that offered no fix changed or vanished", &d1[miss]);
        for (size_t j = 0; j < n_rd && !why; j++) {
            if (!r_d[j].used) why = explain("(a)/(c) after the fix a declaration-pass diagnostic is there that was not (or whose fix did not remove its mistake)", &r_d[j]);
        }
        /* the body pass: strictly unchanged unless the original build stopped, when only new diagnostics may appear (the code did not run before) */
        if (!why) {
            miss = take_all(b1, n_b1, r_b, n_rb, rn, n_rn);
            if (miss >= 0) why = explain("(b) a body diagnostic that offered no fix changed or vanished", &b1[miss]);
            for (size_t j = 0; j < n_rb && !why && !stopped; j++) {
                if (!r_b[j].used) why = explain("(a)/(c) after the fix a diagnostic is there that was not (the original build had not stopped)", &r_b[j]);
            }
        }
        key_free(d1, n_d1);
        key_free(b1, n_b1);
        key_free(r_d, n_rd);
        key_free(r_b, n_rb);
    }
    ir_state_destroy(&st);
    parser_state_destroy(&ps);
    free(fixed);
    free(sp);
    for (size_t i = 0; i < n_rn; i++) {
        free(rn[i].old_name);
        free(rn[i].new_name);
    }
    free(rn);
    (void)m;
    return why;
}
