#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
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
 *     requires status 0). */

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

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: ir_dump <file.mn>\n");
        return 1;
    }
    size_t len;
    char *src = read_file(argv[1], &len);

    /* static: modified after setjmp and read after the longjmp (C11 7.13.2.1; see
     * parser_main.c). */
    static ParserState ps;
    static NodeList items;
    static IrState st;
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
