#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "arena.h"
#include "ast.h"
#include "diag.h"
#include "harness_mode.h"
#include "lexer.h"
#include "parser.h"
#include "parser_state.h"

/* libFuzzer entry point (Step 12, docs/M2_PLAN.md). Same one-token-peek
 * mode dispatch as tools/mannac/parser_main.c's golden-test harness
 * (whole file / block / expression) - literally the same code now
 * (harness_mode.h); it used to be a hand-copied peek that routed every
 * '['-leading input to file mode, so array literals at the start of an
 * input were never fuzzed as expressions. Fuzzing all three through one
 * input is simpler than three separate harnesses, and the dispatch
 * itself is pure and deterministic given the bytes, so there's nothing
 * lost by sharing one entry point. setjmp/longjmp mirrors parser_main.c's
 * fatal path (without the printing): all three "abandon this input"
 * escapes (arena cap, depth cap, error cap) are expected outcomes, not
 * crashes - only an actual trap/ASan/UBSan report, a hang, or (with
 * MANNA_FUZZ_TRACE_LAST_RESORT, see parser.c) reaching the top-level
 * loop's last-resort single-token skip counts as a finding. Not built
 * into the real `manna` CLI or any golden-test harness - a libFuzzer-only
 * artifact, run locally via tools/fuzz/run_fuzzers.sh, never in CI. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ParserState ps;
    parser_state_init(&ps, 16 * 1024 * 1024);
    if (setjmp(ps.fatal_target) != 0) {
        parser_state_destroy(&ps);
        return 0;
    }

    const char *src = (const char *)data;

    HarnessMode mode = harness_pick_mode(src, size);
    bool is_file = (mode == HARNESS_MODE_FILE);
    bool is_block = (mode == HARNESS_MODE_BLOCK);

    Lexer lx;
    lexer_init(&lx, src, size);

    if (is_file) {
        NodeList items;
        parser_parse_file(&ps, &lx, &items);
    } else if (is_block) {
        parser_parse_block(&ps, &lx);
    } else {
        parser_parse_expression(&ps, &lx);
    }

    parser_state_destroy(&ps);
    return 0;
}
