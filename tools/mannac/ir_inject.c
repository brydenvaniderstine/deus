#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"
#include "lexer.h"
#include "parser.h"
#include "parser_state.h"

#ifndef MANNA_POISON_INJECT
#error "ir_inject.c needs -DMANNA_POISON_INJECT (and every IR source built with it)"
#endif

/* The single-fault injection sweep for ONE program (typed-IR step 21b; the step 21 plan, section 3b). Not part of the compiler.
 *
 *   ir_inject [--count | --range A B | --only K] [--trace] <file.mn>
 *
 * The program must parse clean and build clean (IR_BUILT); otherwise it prints `SKIP <why>` and exits 0. A count-only build says how many values the builder
 * creates (N). Then, for every k in 0..N-1, a fresh build makes the k-th value ERROR with nothing reported (ir.h, IR_INJECT_TYPE). The oracle: that build
 * reports ZERO diagnostics and does not crash (the harness is built with ASan and UBSan, so a crash is the process dying). Any diagnostic is a FINDING: a
 * cascade from a poison that was, by construction, already reported. A finding also records whether a fix-it was offered for it (a fix for a mistake
 * that was never reported is wrong by definition) and what the fix self-check says about it.
 * Output: one `FINDING` block per finding, then `RESULT <file> points=N findings=F`. Exit status 1 if F > 0. --trace prints each k to stderr first, so a crash
 * can be attributed (the runner re-runs a crashed program with it). --count prints `COUNT <file> N` and stops; --range A B runs only k in [A, B), so the
 * runner can split a big program across workers without dropping a point. */

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) { perror(path); exit(2); }
    fclose(f);
    *out_len = (size_t)size;
    return buf;
}

static const char *site_name(int site) {
    return site == IR_INJ_EXPR ? "expr" : site == IR_INJ_LOCAL ? "local" : site == IR_INJ_DECL_TYPE ? "decl-type" : "?";
}

/* ---- the collapse report (decision D1) ----
 * In the injected run, a node the builder made ERROR on its own (orig_error, not the injected one) is a parent that gave up. Its span names the same
 * expression in the baseline run; if that expression had a real type there, the injection took a type away from a PARENT. Rows: parent kind and operator,
 * the baseline type, and which child slot the poison sat in. */
typedef struct { Span span; const IrExpr *e; } BaseNode;
static BaseNode *bn;
static size_t bn_n;

static bool span_eq(Span a, Span b) {
    return a.start_line == b.start_line && a.start_col == b.start_col && a.end_line == b.end_line && a.end_col == b.end_col;
}

static const IrExpr *lookup(Span s) { /* the LAST logged node with that span: a parent is created after its children */
    for (size_t i = bn_n; i-- > 0;)
        if (span_eq(bn[i].span, s)) return bn[i].e;
    return NULL;
}

static unsigned long span_key(Span s) {
    return (unsigned long)s.start_line * 1000003UL + (unsigned long)s.start_col * 8191UL + (unsigned long)s.end_line * 131UL + (unsigned long)s.end_col;
}

static const IrExpr **bt_slots;
static size_t bt_cap;

static void bt_build(IrInjectLog *log, size_t n) {
    free(bt_slots);
    bt_cap = 1;
    while (bt_cap < n * 2 + 2) bt_cap *= 2;
    bt_slots = calloc(bt_cap, sizeof(*bt_slots));
    for (size_t i = 0; i < n; i++) { /* later nodes overwrite earlier ones of the same span */
        size_t h = span_key(log[i].e->span) & (bt_cap - 1);
        while (bt_slots[h] && !span_eq(bt_slots[h]->span, log[i].e->span)) h = (h + 1) & (bt_cap - 1);
        bt_slots[h] = log[i].e;
    }
}

static const IrExpr *bt_lookup(Span s) {
    size_t h = span_key(s) & (bt_cap - 1);
    while (bt_slots[h]) {
        if (span_eq(bt_slots[h]->span, s)) return bt_slots[h];
        h = (h + 1) & (bt_cap - 1);
    }
    return NULL;
}

static const char *slot_of(const IrExpr *b, const IrExpr *j, char *buf, size_t cap) {
    if (!j) return "?";
    switch (b->kind) {
        case IR_E_UNARY:  if (b->as.unary.operand == j) return "operand"; break;
        case IR_E_BINARY: if (b->as.binary.lhs == j) return "lhs"; if (b->as.binary.rhs == j) return "rhs"; break;
        case IR_E_CALL:   for (size_t i = 0; i < b->as.call.args.len; i++) if (b->as.call.args.items[i] == j) { snprintf(buf, cap, "arg%zu", i); return buf; } break;
        case IR_E_STRUCT_NEW: for (size_t i = 0; i < b->as.struct_new.values.len; i++) if (b->as.struct_new.values.items[i] == j) { snprintf(buf, cap, "field%zu", i); return buf; } break;
        case IR_E_FIELD:  if (b->as.field.base == j) return "base"; break;
        case IR_E_INDEX:  if (b->as.index.base == j) return "base"; if (b->as.index.idx == j) return "index"; break;
        case IR_E_SLICE:  if (b->as.slice.base == j) return "base"; if (b->as.slice.lo == j) return "lo"; if (b->as.slice.hi == j) return "hi"; break;
        case IR_E_CONVERT: if (b->as.convert.operand == j) return "operand"; break;
        case IR_E_ARRAY:  for (size_t i = 0; i < b->as.array.elems.len; i++) if (b->as.array.elems.items[i] == j) return "element"; break;
        case IR_E_REPEAT: if (b->as.repeat.value == j) return "value"; break;
        case IR_E_LEN:    if (b->as.len.operand == j) return "operand"; break;
        default: break;
    }
    return "below"; /* not a direct child: the parent collapsed because a child of the poisoned node's ancestor chain did */
}

static const char *kind_name(const IrExpr *e) {
    static const char *names[] = {"int", "bool", "local", "unary", "binary", "call", "struct_new", "field", "index", "slice", "convert", "array", "repeat", "len"};
    return names[e->kind];
}

typedef struct { char key[96]; long n; long first_k; } Row;
static Row rows[512];
static size_t nrows;

static void add_row(IrState *st, const IrExpr *b, const char *site, const char *slot, long k) {
    char key[96], op[16] = "";
    if (b->kind == IR_E_BINARY) snprintf(op, sizeof op, "%s", ir_binop_text(b->as.binary.op));
    else if (b->kind == IR_E_UNARY) snprintf(op, sizeof op, "%s", ir_unop_text(b->as.unary.op));
    snprintf(key, sizeof key, "%s %s -> %s  (poison in %s: %s)", kind_name(b), op[0] ? op : "-", ir_type_text(st, b->type), site, slot);
    for (size_t i = 0; i < nrows; i++)
        if (strcmp(rows[i].key, key) == 0) { rows[i].n++; return; }
    if (nrows < 512) { snprintf(rows[nrows].key, sizeof rows[nrows].key, "%s", key); rows[nrows].n = 1; rows[nrows].first_k = k; nrows++; }
}

int main(int argc, char **argv) {
    long only = -1, ra = -1, rb = -1;
    bool count_only = false;
    bool trace = false;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) only = atol(argv[++i]);
        else if (strcmp(argv[i], "--range") == 0 && i + 2 < argc) { ra = atol(argv[i + 1]); rb = atol(argv[i + 2]); i += 2; }
        else if (strcmp(argv[i], "--count") == 0) count_only = true;
        else if (strcmp(argv[i], "--trace") == 0) trace = true;
        else path = argv[i];
    }
    if (!path) { fprintf(stderr, "usage: ir_inject [--count | --range A B | --only K] [--trace] <file.mn>\n"); return 2; }
    size_t len;
    char *src = read_file(path, &len);

    static ParserState ps;
    static NodeList items;
    parser_state_init(&ps, 16 * 1024 * 1024);
    if (setjmp(ps.fatal_target) == 0) {
        Lexer lx;
        lexer_init(&lx, src, len);
        (void)parser_parse_file(&ps, &lx, &items);
    }
    if (ps.diags.len > 0) { printf("SKIP %s parse\n", path); parser_state_destroy(&ps); free(src); return 0; }

    IrState base;
    ir_state_init(&base, IR_ARENA_CAP);
    ir_inject_target = -1;
    ir_inject_seen = 0;
    IrModule *m = NULL;
    IrStatus bs = ir_build(&base, &ps.diags, &items, &m);
    long n = ir_inject_seen;
    if (bs == IR_BUILT) bt_build(ir_inject_log, ir_inject_nlog);
    if (bs != IR_BUILT) {
        ir_state_destroy(&base);
        free(ir_inject_log);
        printf("SKIP %s not-clean\n", path);
        parser_state_destroy(&ps);
        free(src);
        return 0;
    }

    if (count_only) {
        printf("COUNT %s %ld\n", path, n);
        ir_state_destroy(&base);
        free(bt_slots);
        free(ir_inject_log);
        parser_state_destroy(&ps);
        free(src);
        return 0;
    }
    long findings = 0, from = only >= 0 ? only : ra >= 0 ? ra : 0, to = only >= 0 ? only + 1 : ra >= 0 ? (rb < n ? rb : n) : n;
    if (from > to) from = to;
    for (long k = from; k < to; k++) {
        if (trace) fprintf(stderr, "k=%ld\n", k);
        IrState st;
        ir_state_init(&st, IR_ARENA_CAP);
        ir_inject_target = k;
        ir_inject_seen = 0;
        ir_inject_site = 0;
        ir_inject_what = 0;
        ir_inject_nlog = 0;
        IrModule *mm = NULL;
        IrStatus status = ir_build(&st, &ps.diags, &items, &mm);
        int site = ir_inject_site, what = ir_inject_what;
        ir_inject_target = -1; /* the self-check rebuilds the source: no injection there */
        {   /* the collapse report */
            const IrExpr *j = NULL;
            for (size_t i = 0; i < ir_inject_nlog; i++) if (ir_inject_log[i].injected) { j = bt_lookup(ir_inject_log[i].e->span); break; }
            for (size_t i = 0; i < ir_inject_nlog; i++) {
                if (!ir_inject_log[i].orig_error || ir_inject_log[i].injected) continue;
                const IrExpr *b = bt_lookup(ir_inject_log[i].e->span);
                if (!b || b == j || b->type->kind == IR_T_ERROR) continue;
                char buf[16];
                add_row(&base, b, site_name(site), site == IR_INJ_EXPR ? slot_of(b, j, buf, sizeof buf) : "-", k);
            }
        }
        if (status != IR_BUILT) {
            findings++;
            bool has_fix = false;
            for (size_t i = 0; i < st.diags.len; i++) has_fix |= st.diags.items[i].fix != NULL;
            printf("FINDING %s k=%ld site=%s what=%d status=%s diagnostics=%zu fix_offered=%s\n", path, k, site_name(site), what,
                   status == IR_ERRORS ? "errors" : "unimplemented", st.diags.len, has_fix ? "YES" : "no");
            for (size_t i = 0; i < st.diags.len; i++) {
                const Diagnostic *d = &st.diags.items[i];
                printf("  %d:%d %s %s\n", d->span.start_line, d->span.start_col, d->code, d->message);
            }
            if (has_fix) {
                char *why = ir_fix_selfcheck(src, len, &st);
                printf("  fix self-check: %s\n", why ? why : "the offered fix holds (but it answers a mistake nobody reported)");
                free(why);
            }
        }
        ir_state_destroy(&st);
    }
    for (size_t i = 0; i < nrows; i++) printf("COLLAPSE %ld %s\n", rows[i].n, rows[i].key);
    printf("RESULT %s points=%ld findings=%ld\n", path, to - from, findings);
    ir_state_destroy(&base);
    free(bt_slots);
    free(ir_inject_log);
    parser_state_destroy(&ps);
    free(src);
    return findings ? 1 : 0;
}
