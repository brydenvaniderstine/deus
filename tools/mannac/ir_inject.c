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
    ir_state_destroy(&base);
    if (bs != IR_BUILT) { printf("SKIP %s not-clean\n", path); parser_state_destroy(&ps); free(src); return 0; }

    if (count_only) {
        printf("COUNT %s %ld\n", path, n);
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
        IrModule *mm = NULL;
        IrStatus status = ir_build(&st, &ps.diags, &items, &mm);
        int site = ir_inject_site, what = ir_inject_what;
        ir_inject_target = -1; /* the self-check rebuilds the source: no injection there */
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
    printf("RESULT %s points=%ld findings=%ld\n", path, to - from, findings);
    parser_state_destroy(&ps);
    free(src);
    return findings ? 1 : 0;
}
