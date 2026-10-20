#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"
#include "lexer.h"
#include "parser.h"
#include "parser_state.h"

/* libFuzzer entry point for the typed IR (typed-IR step 16, docs/TYPED_IR.md section 6): lexer -> parser -> IR, whole-file
 * mode only (the IR takes a file). Two builds of the same harness:
 *   - gated (default): ir_build refuses a parse that reported any diagnostic, exactly as in use. Mutated inputs rarely
 *     survive the parser, so this mostly exercises the front of the pipeline.
 *   - ungated (-DMANNA_FUZZ_IR_UNGATED, which removes the gate line in ir_build.c): the IR runs on whatever tree the parser
 *     produced even when it reported errors. This is the empirical check that the gate is policy, not a safety dependency.
 *     A parse that ended in a fatal abort (arena, depth or error cap) leaves no usable tree and is not built.
 * Findings are a trap, an ASan/UBSan report, a hang, or IR_UNIMPLEMENTED: the parser cannot build the one ExprKind the
 * IR leaves unhandled, so reaching it (or the two asserts that stand in for the other raisers) is a bug. On IR_BUILT the
 * module is dumped and, on IR_ERRORS, the fixes are applied, so the dumper and the rename path are fuzzed too.
 * A libFuzzer-only artifact, run via tools/fuzz/run_fuzzers.sh, never in CI. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static FILE *sink;
    if (!sink) sink = fopen("/dev/null", "w");

    ParserState ps;
    NodeList items;
    parser_state_init(&ps, 16 * 1024 * 1024);
    node_list_init(&items);
    int fatal = setjmp(ps.fatal_target);
    if (fatal == 0) {
        Lexer lx;
        lexer_init(&lx, (const char *)data, size);
        (void)parser_parse_file(&ps, &lx, &items);
    }
#ifdef MANNA_FUZZ_IR_UNGATED
    if (fatal != 0) {
        parser_state_destroy(&ps);
        return 0;
    }
#endif

    IrState st;
    ir_state_init(&st, IR_ARENA_CAP);
    IrModule *m = NULL;
    IrStatus status = ir_build(&st, &ps.diags, &items, &m);
    if (status == IR_BUILT) {
        ir_dump_module(sink, m);
    } else if (status == IR_ERRORS) {
        char *copy = malloc(size + 1); /* the lexer is handed exactly `size` bytes above; this copy is NUL-terminated for the fixer */
        memcpy(copy, data, size);
        copy[size] = '\0';
        char *fixed = ir_apply_fixes(copy, size, &st.diags);
        free(fixed);
        free(copy);
    } else if (status == IR_UNIMPLEMENTED) {
        abort();
    }
    ir_state_destroy(&st);
    parser_state_destroy(&ps);
    return 0;
}
