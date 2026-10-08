#include <stddef.h>
#include <stdint.h>
#include "lexer.h"

/* libFuzzer entry point (Step 12, docs/M2_PLAN.md). Tokenizes arbitrary
 * bytes to EOF - the lexer has no escape point of its own (no arena, no
 * longjmp; diag.h's dedicated lexer errors are just tokens), so there's
 * nothing to set up beyond lexer_init itself. Not built into the real
 * `manna` CLI or any golden-test harness - a libFuzzer-only artifact, run
 * locally via tools/fuzz/run_fuzzers.sh, never in CI. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    Lexer lx;
    lexer_init(&lx, (const char *)data, size);
    for (;;) {
        Token t = lexer_next(&lx);
        if (t.kind == TOK_EOF) {
            break;
        }
    }
    return 0;
}
