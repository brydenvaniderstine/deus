#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"
#include "lexer.h"
#include "parser.h"
#include "parser_state.h"

/* Test-only harness for the typed-IR goldens (tests/ir: *.mn in, *.ir out).
 * Not the `manna` CLI; the IR counterpart of parser_main.c. Whole-file mode
 * only (an expression or block needs a symbol context the IR stage supplies
 * itself). Output is exactly one of:
 *   - the IR dump (IR_BUILT);
 *   - `ERRORS:` and the diagnostics, each line `L:C CODE message` (IR goldens
 *     print codes, unlike the parser's), then for a gated file a blank line and
 *     `IR: skipped (parse errors)`;
 *   - `IR: unimplemented` and exit status 3 (IR_UNIMPLEMENTED: the skeleton cannot
 *     build this construct yet - no golden can record that, because the runner
 *     requires status 0).
 * `ir_dump --fixed <file.mn>` prints the source with EVERY fix the diagnostics carry applied (typed-IR step 14;
 * tests/ir_fix): the file unchanged if it has none, and for a file the parser or the IR could not accept at all.
 * The runner re-checks the printed source.
 * The fixed text is written with its true length, so a source with NUL bytes (a comment can hold any byte) is not truncated (register C3, fixed in
 * step 20f when the fuzz self-check found it). */

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        perror(path);
        exit(1);
    }
    fclose(f);
    *out_len = (size_t)size;
    return buf;
}

static void print_diags(const DiagList *diags) {
    printf("ERRORS:\n");
    for (size_t i = 0; i < diags->len; i++) {
        const Diagnostic *d = &diags->items[i];
        printf("%d:%d %s %s\n", d->span.start_line, d->span.start_col, d->code, d->message);
    }
}

/* One file: the whole of the harness for it. Returns the exit status (0, 3 for IR_UNIMPLEMENTED, 4 for a failed fix self-check). Everything it allocates it frees on every
 * path, so `--batch` can call it once per file. */
static int run_file(const char *path, bool fixed) {
    size_t len;
    char *src = read_file(path, &len);

    /* static: modified after setjmp and read after the longjmp (C11 7.13.2.1; see
     * parser_main.c). */
    static ParserState ps;
    static NodeList items;
    static IrState st;
    items = (NodeList){0}; /* a batch run reuses these: a fresh list for every file */
    parser_state_init(&ps, 16 * 1024 * 1024);
    ir_state_init(&st, IR_ARENA_CAP);

    if (setjmp(ps.fatal_target) == 0) {
        Lexer lx;
        lexer_init(&lx, src, len);
        (void)parser_parse_file(&ps, &lx, &items); /* the gate reads ps.diags, not this result */
    }

    IrModule *m = NULL;
    IrStatus status = ir_build(&st, &ps.diags, &items, &m);
    int rc = 0;
    /* The self-check (step 20f): every fix the diagnostics carry must hold up (ir.h ir_fix_selfcheck). A failure is a bug in the compiler: say so
     * loudly and exit 4, in both modes, so no golden, fix case, property program or fuzz input can pass with a wrong fix. */
    if (status == IR_ERRORS) {
        char *why = ir_fix_selfcheck(src, len, &st);
        if (why) {
            if (!fixed) print_diags(&st.diags);
            fprintf(stderr, "ir_dump: internal error: a fix-it did not re-check (please report this): %s\n", why);
            free(why);
            ir_state_destroy(&st);
            parser_state_destroy(&ps);
            free(src);
            return 4;
        }
    }
    if (fixed) {
        size_t out_len = 0;
        char *out = status == IR_ERRORS ? ir_apply_fixes_n(src, len, &st.diags, &out_len) : NULL;
        fwrite(out ? out : src, 1, out ? out_len : len, stdout);
        free(out);
        ir_state_destroy(&st);
        parser_state_destroy(&ps);
        free(src);
        return 0;
    }
    switch (status) {
        case IR_BUILT:
            ir_dump_module(stdout, m);
            break;
        case IR_SKIPPED:
            print_diags(&ps.diags);
            printf("\nIR: skipped (parse errors)\n");
            break;
        case IR_ERRORS:
            print_diags(&st.diags);
            break;
        case IR_UNIMPLEMENTED:
            printf("IR: unimplemented\n");
            fprintf(stderr, "ir_dump: not implemented yet: %s\n", st.unimplemented);
            rc = 3;
            break;
    }
    ir_state_destroy(&st);
    parser_state_destroy(&ps);
    free(src);
    return rc;
}

/* `ir_dump [--fixed] <file.mn>` is the golden harness. `ir_dump --batch [--fixed] <file.mn>...` runs the same code on many files in ONE process (the no-cascade proof
 * runs hundreds of candidates on every mutant, and a sanitizer's start-up would dominate): each file's output is framed by `@@@ BEGIN <path>` and `@@@ END <status>` (on a line of its own after one added newline), and the
 * first file whose status is not 0 ends the batch with that status; a process that dies leaves a BEGIN without its END, which names the file. */
int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "--batch") == 0) {
        int first = 2;
        bool fixed = strcmp(argv[2], "--fixed") == 0;
        if (fixed) first = 3;
        for (int i = first; i < argc; i++) {
            printf("@@@ BEGIN %s\n", argv[i]);
            fflush(stdout);
            int rc = run_file(argv[i], fixed);
            printf("\n@@@ END %d\n", rc); /* the leading newline is the frame's: a --fixed output may not end in one */
            fflush(stdout);
            if (rc != 0) return rc;
        }
        return 0;
    }
    bool fixed = argc == 3 && strcmp(argv[1], "--fixed") == 0;
    if (argc != 2 && !fixed) {
        fprintf(stderr, "usage: ir_dump [--fixed] <file.mn> | ir_dump --batch [--fixed] <file.mn>...\n");
        return 1;
    }
    return run_file(argv[fixed ? 2 : 1], fixed);
}

