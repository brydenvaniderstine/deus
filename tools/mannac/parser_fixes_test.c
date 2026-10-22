#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "harness_mode.h"
#include "parser.h"

/* Fresh-eyes audit triage (Phase A), covered here rather than as golden
 * tests because each one needs either a synthetic Token parser_dump's
 * harness has no way to construct (#16's multi-line token_span), direct
 * access to an AST node field the S-expression dump never prints (#7's
 * `parenthesized` flag - deliberately not given its own dump syntax,
 * since there's no Paren AST node), or a dispatch path parser_dump's own
 * one-token peek can never reach (#16's lexer-error-first-token: a file
 * starting with a lexer error never peeks as 'manna'/'struct'/'fn'/'[',
 * so parser_dump always routes it to expression mode, never to
 * parser_parse_file - see parser_main.c's own comment on that peek). */

static void test_token_span_single_line_unchanged(void) {
    Token t = {0};
    t.kind = TOK_IDENT;
    t.lexeme = "foo";
    t.lexeme_len = 3;
    t.line = 2;
    t.col = 4;

    Span s = token_span(t);
    assert(s.start_line == 2 && s.start_col == 4);
    assert(s.end_line == 2 && s.end_col == 7); /* col + lexeme_len, as before */
    printf("PASS: token_span - single-line token unaffected by the multi-line fix\n");
}

static void test_token_span_multiline_unterminated_comment(void) {
    /* Mirrors lexer.c's own unterminated-block-comment token shape: the
     * lexeme runs from the opening delimiter to EOF and can contain
     * embedded newlines - the one token kind today where lexeme_len
     * alone (a raw byte count) isn't enough to compute an end position,
     * since most of those bytes belong to lines other than the start
     * line. */
    Token t = {0};
    t.kind = TOK_ERROR_UNTERMINATED_COMMENT;
    t.lexeme = "/*\nfoo\n"; /* 7 bytes: '\n' at offsets 2 and 6 */
    t.lexeme_len = strlen(t.lexeme);
    t.line = 5;
    t.col = 10;
    t.error_msg = "unterminated block comment";

    Span s = token_span(t);
    assert(s.start_line == 5 && s.start_col == 10);
    /* 2 embedded newlines -> ends 2 lines down; 1 byte ("\n") follows the
     * last embedded newline in the lexeme, so the final line this token
     * covers has exactly 0 real characters on it before the end - the
     * exclusive end column is 1 (nothing between the line start and the
     * end of the token on that line). */
    assert(s.end_line == 7);
    assert(s.end_col == 1);
    printf("PASS: token_span - multi-line lexeme (unterminated comment) computes a real end line/col, not col + raw byte count\n");
}

static void test_token_span_multiline_trailing_text(void) {
    /* Same shape, but with real trailing content after the last '\n', to
     * confirm the end column reflects THAT text's length, not always 1. */
    Token t = {0};
    t.kind = TOK_ERROR_UNTERMINATED_COMMENT;
    t.lexeme = "/*\nabcde"; /* 8 bytes: one '\n' at offset 2, "abcde" (5 chars) after it */
    t.lexeme_len = strlen(t.lexeme);
    t.line = 1;
    t.col = 1;
    t.error_msg = "unterminated block comment";

    Span s = token_span(t);
    assert(s.start_line == 1 && s.start_col == 1);
    assert(s.end_line == 2);   /* one embedded newline */
    assert(s.end_col == 6);   /* "abcde" is 5 chars, columns 1-5, exclusive end 6 */
    printf("PASS: token_span - multi-line lexeme end column reflects the final line's own length\n");
}

/* The returned Expr points into ps's arena, so the CALLER owns ps and must
 * parser_state_destroy it once done with the node (an earlier version
 * leaked the arena "deliberately" - `leaks`/LSan flagged it, R7). */
static Expr *parse_one_expression(const char *src, ParserState *ps) {
    parser_state_init(ps, 1 << 20);
    if (setjmp(ps->fatal_target) != 0) {
        fprintf(stderr, "FAIL: unexpected fatal longjmp parsing \"%s\"\n", src);
        exit(1);
    }
    Lexer lx;
    lexer_init(&lx, src, strlen(src));
    Expr *e = parser_parse_expression(ps, &lx);
    assert(e != NULL);
    assert(ps->diags.len == 0);
    return e;
}

static void test_parenthesized_flag_direct_literal(void) {
    /* "-9223372036854775808": a literal directly negated, no parens in
     * between - the i64::MIN pattern the IR stage (docs/M2_PLAN.md
     * records this) must recognize via operand->parenthesized == false,
     * not by re-deriving it some other way. */
    ParserState ps;
    Expr *e = parse_one_expression("-9223372036854775808", &ps);
    assert(e->kind == EXPR_UNARY);
    assert(e->as.unary.op == UNOP_NEG);
    Expr *operand = e->as.unary.operand;
    assert(operand->kind == EXPR_NUMBER);
    assert(operand->as.number.value == 9223372036854775808ULL);
    assert(operand->parenthesized == false);
    assert(operand->span.start_col == 2);  /* right after the '-' */
    assert(operand->span.end_col == 21);   /* just the 19 digits, no parens */
    parser_state_destroy(&ps);
    printf("PASS: parenthesized flag - a directly-negated literal is NOT marked parenthesized\n");
}

static void test_parenthesized_flag_wrapped_literal(void) {
    /* "-(9223372036854775808)": the SAME magnitude, but through a
     * parenthesized sub-expression - docs/M2_PLAN.md records that the IR
     * stage must treat this as a plain overflow error, not i64::MIN,
     * and this flag plus the widened span are the mechanism that lets it
     * tell the two apart (no dedicated Paren AST node exists to hang
     * this fact on instead). */
    ParserState ps;
    Expr *e = parse_one_expression("-(9223372036854775808)", &ps);
    assert(e->kind == EXPR_UNARY);
    assert(e->as.unary.op == UNOP_NEG);
    Expr *operand = e->as.unary.operand;
    assert(operand->kind == EXPR_NUMBER);
    assert(operand->as.number.value == 9223372036854775808ULL);
    assert(operand->parenthesized == true);
    assert(operand->span.start_col == 2);  /* the '(' itself */
    assert(operand->span.end_col == 23);   /* through the ')', not just the digits */
    parser_state_destroy(&ps);
    printf("PASS: parenthesized flag - a parenthesized literal IS marked parenthesized, span widened to include the parens\n");
}

static void test_version_decl_lexer_error_not_e0107(void) {
    /* A file whose very FIRST token is a lexer error (here: '@', an
     * unrecognized byte) must not be reported as "missing version
     * header" (E0107) - that diagnostic's own fix-it ("insert manna v0;
     * at 1:1") would be actively misleading over text that isn't valid
     * source to begin with. It should get the generic E0105, naming the
     * actual problem (token_describe already renders a lexer-error
     * token's own message, e.g. "unexpected character"). */
    const char *src = "@ struct Foo {}";
    ParserState ps;
    parser_state_init(&ps, 1 << 20);
    if (setjmp(ps.fatal_target) != 0) {
        fprintf(stderr, "FAIL: unexpected fatal longjmp\n");
        exit(1);
    }
    Lexer lx;
    lexer_init(&lx, src, strlen(src));
    NodeList items;
    bool ok = parser_parse_file(&ps, &lx, &items);
    assert(!ok);
    assert(ps.diags.len == 1);
    assert(strcmp(ps.diags.items[0].code, "E0105") == 0);
    assert(strstr(ps.diags.items[0].message, "unexpected character") != NULL);
    parser_state_destroy(&ps);
    printf("PASS: version_decl - a lexer-error first token reports E0105, not E0107\n");
}

/* R6 (external audit of da9f01c): a ParserState reused for a second parse
 * kept the PREVIOUS parse's depth and error_count. After a fatal longjmp
 * (E0103 here) depth is left at 257, so the next, perfectly valid file
 * hit a spurious E0103 on its very first depth_enter. Phase A's #17 only
 * reset the sync_* counters and recovered_past_body. All per-parse state
 * now resets at every public parse entry. */
static void test_state_reset_after_fatal_longjmp(void) {
    enum { LEVELS = 300 };
    static char deep[2 * LEVELS + 64];
    size_t n = 0;
    n += (size_t)snprintf(deep + n, sizeof deep - n, "manna v0;\nfn f() {\n    let a = ");
    for (int i = 0; i < LEVELS; i++) deep[n++] = '(';
    deep[n++] = 'x';
    for (int i = 0; i < LEVELS; i++) deep[n++] = ')';
    n += (size_t)snprintf(deep + n, sizeof deep - n, ";\n}\n");

    /* static: read after the longjmp (C11 7.13.2.1: an automatic non-volatile
     * object changed after setjmp is indeterminate there; see parser_main.c). */
    static ParserState ps;
    parser_state_init(&ps, 1 << 22);
    Lexer lx1;
    lexer_init(&lx1, deep, n);
    NodeList items1;
    if (setjmp(ps.fatal_target) == 0) {
        parser_parse_file(&ps, &lx1, &items1);
        fprintf(stderr, "FAIL: expected the 300-level input to hit E0103\n");
        exit(1);
    }
    assert(ps.diags.len == 1 && strcmp(ps.diags.items[0].code, "E0103") == 0);
    assert(ps.depth > 0); /* stale, left behind by the longjmp - the precondition of the bug */

    const char *ok = "manna v0;\nfn g() {\n    let b = 1;\n}\n";
    Lexer lx2;
    lexer_init(&lx2, ok, strlen(ok));
    NodeList items2;
    if (setjmp(ps.fatal_target) != 0) {
        fprintf(stderr, "FAIL: valid file hit a fatal error after the earlier longjmp (depth not reset)\n");
        exit(1);
    }
    bool good = parser_parse_file(&ps, &lx2, &items2);
    assert(good);
    assert(items2.len == 1);
    assert(ps.diags.len == 1); /* still only the first parse's E0103: nothing new */
    assert(ps.depth == 0 && ps.error_count == 0);
    parser_state_destroy(&ps);
    printf("PASS: parse entry - depth reset after a fatal longjmp; a valid file parses clean\n");
}

static void test_state_reset_error_count_between_parses(void) {
    const char *bad = "manna v0;\nfn f() {\n    let = 1;\n    let = 2;\n    let = 3;\n}\n";
    const char *ok = "manna v0;\nfn g() {\n    let b = 1;\n}\n";
    ParserState ps;
    parser_state_init(&ps, 1 << 20);
    if (setjmp(ps.fatal_target) != 0) {
        fprintf(stderr, "FAIL: unexpected fatal longjmp\n");
        exit(1);
    }
    Lexer lx1;
    lexer_init(&lx1, bad, strlen(bad));
    NodeList items1;
    parser_parse_file(&ps, &lx1, &items1);
    assert(ps.error_count == 3);
    Lexer lx2;
    lexer_init(&lx2, ok, strlen(ok));
    NodeList items2;
    assert(parser_parse_file(&ps, &lx2, &items2));
    assert(ps.error_count == 0); /* not 3: it counts THIS parse's errors */
    assert(ps.diags.len == 3);
    parser_state_destroy(&ps);
    printf("PASS: parse entry - error_count restarts at 0 for each parse\n");
}

/* Phase A #17 had no test of its own: the sync_* counters are bumped on
 * entry to each recovery level and dropped on exit, and a fatal longjmp
 * (E0103 deep inside a function body) skips every drop. They must read 0
 * again after the NEXT parse, not carry the abandoned parse's levels. */
static void test_sync_counters_reset_at_parse_entry(void) {
    enum { LEVELS = 300 };
    static char deep[2 * LEVELS + 64];
    size_t n = 0;
    n += (size_t)snprintf(deep + n, sizeof deep - n, "manna v0;\nfn f() {\n    let a = ");
    for (int i = 0; i < LEVELS; i++) deep[n++] = '(';
    deep[n++] = 'x';
    for (int i = 0; i < LEVELS; i++) deep[n++] = ')';
    n += (size_t)snprintf(deep + n, sizeof deep - n, ";\n}\n");

    /* static: read after the longjmp (C11 7.13.2.1: an automatic non-volatile
     * object changed after setjmp is indeterminate there; see parser_main.c). */
    static ParserState ps;
    parser_state_init(&ps, 1 << 22);
    Lexer lx1;
    lexer_init(&lx1, deep, n);
    NodeList items1;
    if (setjmp(ps.fatal_target) == 0) {
        parser_parse_file(&ps, &lx1, &items1);
        fprintf(stderr, "FAIL: expected E0103\n");
        exit(1);
    }
    /* precondition: the abandoned parse really left recovery levels active */
    assert(ps.sync_top_level > 0 && ps.sync_block > 0 && ps.sync_statement > 0);

    const char *ok = "manna v0;\nfn g() {}\n";
    Lexer lx2;
    lexer_init(&lx2, ok, strlen(ok));
    NodeList items2;
    if (setjmp(ps.fatal_target) != 0) {
        fprintf(stderr, "FAIL: fatal on the second parse\n");
        exit(1);
    }
    assert(parser_parse_file(&ps, &lx2, &items2));
    assert(ps.sync_top_level == 0 && ps.sync_block == 0 && ps.sync_statement == 0 &&
           ps.sync_header == 0 && ps.sync_for_header == 0 && ps.recovered_past_body == false);
    parser_state_destroy(&ps);
    printf("PASS: parse entry - sync_* counters and recovered_past_body are 0 after a parse that followed an abandoned one\n");
}

/* Post-audit finding #0: an unclosed block that meets `fn`/`struct` reports
 * once per token, not once per enclosing block (ParserState remembers the
 * position of the last report). That memory is per-parse state: parsing the
 * SAME stray-fn text a second time with the same ParserState must report
 * the unclosed block again, not treat it as a repeat of the first parse's. */
static void test_unclosed_block_report_resets_between_parses(void) {
    const char *bad = "manna v0;\nfn f() {\n    let x = 1;\nfn g() {}\n";
    ParserState ps;
    parser_state_init(&ps, 1 << 20);
    if (setjmp(ps.fatal_target) != 0) {
        fprintf(stderr, "FAIL: unexpected fatal longjmp\n");
        exit(1);
    }
    for (int round = 1; round <= 2; round++) {
        Lexer lx;
        lexer_init(&lx, bad, strlen(bad));
        NodeList items;
        parser_parse_file(&ps, &lx, &items);
        assert(items.len == 2); /* f kept (partial) and g */
        assert(ps.diags.len == (size_t)round);
        assert(strcmp(ps.diags.items[round - 1].code, "E0105") == 0);
        assert(ps.diags.items[round - 1].span.start_line == 4);
    }
    assert(ps.unclosed_line == 4 && ps.unclosed_col == 1); /* precondition of the reset below */
    const char *ok = "manna v0;\nfn h() {}\n";
    Lexer lx;
    lexer_init(&lx, ok, strlen(ok));
    NodeList items;
    assert(parser_parse_file(&ps, &lx, &items));
    assert(ps.unclosed_line == 0 && ps.unclosed_col == 0);
    parser_state_destroy(&ps);
    printf("PASS: unclosed-block report - once per token, and the memory resets at parse entry\n");
}

/* External audit of v0, finding 2: arena exhaustion longjmp'd with nothing
 * recorded - empty ERRORS, no tree. It now records E0110 (no fix-it) first.
 * Both shapes the audit used (a 300,000-element array literal and a flat
 * 300,000-argument call, ~900 KB each, against the real 16 MiB cap), plus a
 * small arena so the same path is also exercised cheaply. The input is
 * generated here, not committed. */
static char *gen_list_input(const char *prefix, const char *open, const char *close, size_t n, size_t *out_len) {
    size_t cap = strlen(prefix) + strlen(open) + strlen(close) + n * 3 + 64;
    char *buf = malloc(cap);
    size_t len = (size_t)snprintf(buf, cap, "%s%s", prefix, open);
    for (size_t i = 0; i < n; i++) {
        len += (size_t)snprintf(buf + len, cap - len, i ? ", 0" : "0");
    }
    len += (size_t)snprintf(buf + len, cap - len, "%s", close);
    *out_len = len;
    return buf;
}

/* want_line/want_col: where E0110 must point - the last token the parser had
 * read when the arena ran out (fixes-audit finding 5: only "some position"
 * was asserted before). These are exact for this arena size and these node
 * sizes (LP64); a change to either is a deliberate re-pin, not noise. */
static void check_e0110(const char *src, size_t len, size_t arena_size, int want_line, int want_col,
                        const char *what) {
    /* static: read after the longjmp (C11 7.13.2.1: an automatic non-volatile
     * object changed after setjmp is indeterminate there; see parser_main.c). */
    static ParserState ps;
    parser_state_init(&ps, arena_size);
    Lexer lx;
    lexer_init(&lx, src, len);
    NodeList items;
    if (setjmp(ps.fatal_target) == 0) {
        parser_parse_file(&ps, &lx, &items);
        fprintf(stderr, "FAIL: %s: expected the arena to run out\n", what);
        exit(1);
    }
    assert(ps.diags.len == 1);
    const Diagnostic *d = &ps.diags.items[0];
    assert(strcmp(d->code, "E0110") == 0);
    assert(strcmp(d->message, "input too large: the compiler's memory limit was exceeded") == 0);
    assert(d->fix == NULL && d->expected == NULL && d->found == NULL);
    if (d->span.start_line != want_line || d->span.start_col != want_col) {
        fprintf(stderr, "FAIL: %s: E0110 at %d:%d, want %d:%d\n", what, d->span.start_line, d->span.start_col,
                want_line, want_col);
        exit(1);
    }
    assert(d->span.end_line == d->span.start_line && d->span.end_col == d->span.start_col); /* a point, not a range */
    parser_state_destroy(&ps);
}

static void test_arena_exhaustion_reports_e0110(void) {
    size_t len;
    char *big_array = gen_list_input("manna v0;\nfn f() {\n    let a = ", "[", "];\n}\n", 300000, &len);
    check_e0110(big_array, len, 16 * 1024 * 1024, 3, 523620, "300,000-element array literal");
    free(big_array);
    char *big_call = gen_list_input("manna v0;\nfn f() {\n    g(", "", ");\n}\n", 300000, &len);
    check_e0110(big_call, len, 16 * 1024 * 1024, 3, 523613, "flat 300,000-argument call");
    free(big_call);
    char *small = gen_list_input("manna v0;\nfn f() {\n    let a = ", "[", "];\n}\n", 20000, &len);
    check_e0110(small, len, 256 * 1024, 3, 6159, "20,000-element array against a 256 KiB arena");
    free(small);
    printf("PASS: arena exhaustion records exactly one E0110 (array, flat call, small arena), no fix-it\n");
}

/* External audit of v0, finding 4: the fuzz target had its own copy of the
 * mode peek and sent every '['-leading input to file mode. Both harnesses
 * now call harness_pick_mode (harness_mode.h); this pins its decisions,
 * the '[' ones in particular. */
static void test_harness_mode_routing(void) {
    struct { const char *src; HarnessMode want; } cases[] = {
        {"manna v0;\nfn f() {}\n", HARNESS_MODE_FILE},
        {"fn f() {}", HARNESS_MODE_FILE},
        {"struct S { a: u8, }", HARNESS_MODE_FILE},
        {"[inline] fn f() {}", HARNESS_MODE_FILE},
        {"[a][b] fn f() {}", HARNESS_MODE_FILE},
        {"{ a; }", HARNESS_MODE_BLOCK},
        {"[1, 2][0]", HARNESS_MODE_EXPRESSION},
        {"[1, 2, 3]", HARNESS_MODE_EXPRESSION},
        {"[v; 4]", HARNESS_MODE_EXPRESSION},
        {"[a]", HARNESS_MODE_EXPRESSION},
        {"[a][b]", HARNESS_MODE_EXPRESSION},
        {"[a] [b] c", HARNESS_MODE_EXPRESSION},
        {"[", HARNESS_MODE_EXPRESSION},
        {"a[1]", HARNESS_MODE_EXPRESSION},
        {"u8(x)", HARNESS_MODE_EXPRESSION},
        {"", HARNESS_MODE_EXPRESSION},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        HarnessMode got = harness_pick_mode(cases[i].src, strlen(cases[i].src));
        if (got != cases[i].want) {
            fprintf(stderr, "FAIL: mode for \"%s\": got %d, want %d\n", cases[i].src, (int)got, (int)cases[i].want);
            exit(1);
        }
    }
    printf("PASS: harness mode routing - attribute prefixes are file mode, other '[' inputs are expressions\n");
}

/* Fixes-audit finding 4: the overflow hook runs once (in_hook) and releases
 * the reserved tail (reserve = 0), and nothing re-armed either, so a
 * ParserState REUSED after one overflow recorded no E0110 the second time -
 * the unwind was silent again. Every parse entry re-arms the hook now
 * (parser_state_reset_for_parse). The arena itself stays exhausted; what is
 * pinned is that the second overflow is reported like the first. */
static void test_e0110_rearmed_on_state_reuse(void) {
    size_t len;
    char *src = gen_list_input("manna v0;\nfn f() {\n    let a = ", "[", "];\n}\n", 20000, &len);
    static ParserState ps; /* read after the longjmp (C11 7.13.2.1) */
    parser_state_init(&ps, 256 * 1024);
    for (int round = 1; round <= 3; round++) {
        Lexer lx;
        lexer_init(&lx, src, len);
        NodeList items;
        if (setjmp(ps.fatal_target) == 0) {
            parser_parse_file(&ps, &lx, &items);
            fprintf(stderr, "FAIL: round %d: expected the arena to run out\n", round);
            exit(1);
        }
        if (ps.diags.len != (size_t)round) {
            fprintf(stderr, "FAIL: round %d: %zu diagnostics, want %d (E0110 not recorded again)\n",
                    round, ps.diags.len, round);
            exit(1);
        }
        assert(strcmp(ps.diags.items[round - 1].code, "E0110") == 0);
    }
    parser_state_destroy(&ps);
    free(src);
    printf("PASS: E0110 is recorded on every overflow of a reused ParserState (3 rounds)\n");
}

/* E0102's JSON `expected` field named '{' at every call site, but a `for`
 * header's condition clause is terminated by ';', not '{' (known inaccuracy
 * recorded in docs/error_codes.md since r1 Phase C; message, span and the
 * "==" fix-it were always right). Goldens print message text only, so the
 * field is pinned here. */
static const char *e0102_expected(const char *src) {
    ParserState ps;
    static char buf[16];
    parser_state_init(&ps, 1 << 20);
    if (setjmp(ps.fatal_target) != 0) {
        fprintf(stderr, "FAIL: unexpected fatal longjmp parsing \"%s\"\n", src);
        exit(1);
    }
    Lexer lx;
    lexer_init(&lx, src, strlen(src));
    parser_parse_block(&ps, &lx);
    assert(ps.diags.len >= 1);
    assert(strcmp(ps.diags.items[0].code, "E0102") == 0);
    assert(ps.diags.items[0].fix != NULL);
    assert(ps.diags.items[0].fix->n_edits == 1);
    assert(strcmp(ps.diags.items[0].fix->edits[0].text, "==") == 0);
    snprintf(buf, sizeof buf, "%s", ps.diags.items[0].expected);
    parser_state_destroy(&ps);
    return buf;
}

static void test_e0102_expected_field_per_site(void) {
    assert(strcmp(e0102_expected("{ if x = 1 { } }"), "'{'") == 0);
    assert(strcmp(e0102_expected("{ while x = 1 { } }"), "'{'") == 0);
    assert(strcmp(e0102_expected("{ for let i = 0; i = 5; i = i + 1 { } }"), "';'") == 0);
    printf("PASS: E0102 - expected field is '{' for if/while and ';' for a for condition\n");
}

/* E0108's fix-it (#4 step 5): two zero-width inserts, `(` at the start of
 * the comparison-class (<, <=, >, >=) pair's left operand and `)` at the end
 * of its right operand. Goldens never print fix-its, so the exact positions
 * are pinned here. Positions are 1-based line:col; an insert at 1:6 goes
 * BEFORE the character at column 6, i.e. just after a token ending at col 6
 * (end columns are exclusive). */
static void check_mixed_fix(const char *src, int l1, int c1, int l2, int c2) {
    ParserState ps;
    parser_state_init(&ps, 1 << 20);
    if (setjmp(ps.fatal_target) != 0) {
        fprintf(stderr, "FAIL: unexpected fatal longjmp parsing \"%s\"\n", src);
        exit(1);
    }
    Lexer lx;
    lexer_init(&lx, src, strlen(src));
    Expr *e = parser_parse_expression(&ps, &lx);
    if (e != NULL || ps.diags.len != 1 || strcmp(ps.diags.items[0].code, "E0108") != 0) {
        fprintf(stderr, "FAIL: expected exactly one E0108 for \"%s\"\n", src);
        exit(1);
    }
    const DiagFix *f = ps.diags.items[0].fix;
    assert(f != NULL && f->n_edits == 2);
    assert(strcmp(f->edits[0].text, "(") == 0 && strcmp(f->edits[1].text, ")") == 0);
    /* both edits are pure insertions: zero-width spans */
    assert(f->edits[0].span.start_line == f->edits[0].span.end_line);
    assert(f->edits[0].span.start_col == f->edits[0].span.end_col);
    assert(f->edits[1].span.start_line == f->edits[1].span.end_line);
    assert(f->edits[1].span.start_col == f->edits[1].span.end_col);
    if (f->edits[0].span.start_line != l1 || f->edits[0].span.start_col != c1 ||
        f->edits[1].span.start_line != l2 || f->edits[1].span.start_col != c2) {
        fprintf(stderr, "FAIL: \"%s\": '(' at %d:%d, ')' at %d:%d; expected %d:%d and %d:%d\n", src,
                f->edits[0].span.start_line, f->edits[0].span.start_col,
                f->edits[1].span.start_line, f->edits[1].span.start_col, l1, c1, l2, c2);
        exit(1);
    }
    parser_state_destroy(&ps);
}

static void test_e0108_fix_positions(void) {
    /* comparison first: the pair is (a, b)        a < b == c -> (a < b) == c */
    check_mixed_fix("a < b == c", 1, 1, 1, 6);
    /* equality first: the pair is (b, c)          a == b < c -> a == (b < c) */
    check_mixed_fix("a == b < c", 1, 6, 1, 11);
    /* compound operands are wrapped whole         (x + 1 >= y) != z */
    check_mixed_fix("x + 1 >= y != z", 1, 1, 1, 11);
    /* a parenthesized operand: its span includes the parens, so '(' goes
     * BEFORE the existing '(' and ')' AFTER b:    ((a) < b) == c */
    check_mixed_fix("(a) < b == c", 1, 1, 1, 8);
    /* parenthesized right operand of the pair:    a == ((b) < (c)) */
    check_mixed_fix("a == (b) < (c)", 1, 6, 1, 15);
    /* a multi-line left operand: the ')' lands on the LAST line, after b */
    check_mixed_fix("a +\n  1 < b == c", 1, 1, 2, 8);
    /* a multi-line right operand of the equality-first pair */
    check_mixed_fix("a == b +\n  1 < c", 1, 6, 2, 8);
    /* third operator present: still one diagnostic, pair is the first comparison */
    check_mixed_fix("a < b == c < d", 1, 1, 1, 6);
    printf("PASS: E0108 - fix-it is two zero-width inserts at the exact pair boundaries (incl. parenthesized and multi-line operands)\n");
}

/* E0109's fix-it (#4 step 6). `-> unit {` on one line with only blanks
 * between deletes exactly the text "-> unit" with ONE edit; a newline or a
 * comment between makes it TWO edits (the `->` token, the `unit` token) so
 * the comment / line break survives. Positions are line:col (end exclusive)
 * in a file whose line 1 is the version header. The function must STAY in
 * the tree with no return type. */
static void check_return_unit_fix(const char *body, int n_edits, int a_l, int a_c1, int a_c2,
                                  int u_l, int u_c1, int u_c2) {
    char src[256];
    snprintf(src, sizeof src, "manna v0;\n%s", body);
    ParserState ps;
    parser_state_init(&ps, 1 << 20);
    if (setjmp(ps.fatal_target) != 0) {
        fprintf(stderr, "FAIL: unexpected fatal longjmp\n");
        exit(1);
    }
    Lexer lx;
    lexer_init(&lx, src, strlen(src));
    NodeList items;
    bool ok = parser_parse_file(&ps, &lx, &items);
    assert(ok);
    assert(ps.diags.len == 1 && strcmp(ps.diags.items[0].code, "E0109") == 0);
    assert(items.len == 1); /* the function stays in the tree */
    assert(((Decl *)items.items[0])->as.function_decl.return_type == NULL);
    const Diagnostic *d = &ps.diags.items[0];
    assert(d->span.start_line == a_l && d->span.start_col == a_c1);
    assert(d->span.end_line == u_l && d->span.end_col == u_c2);
    const DiagFix *f = d->fix;
    assert(f != NULL && f->n_edits == n_edits);
    for (int i = 0; i < n_edits; i++) assert(strcmp(f->edits[i].text, "") == 0); /* pure deletions */
    if (n_edits == 1) {
        assert(f->edits[0].span.start_line == a_l && f->edits[0].span.start_col == a_c1);
        assert(f->edits[0].span.end_line == u_l && f->edits[0].span.end_col == u_c2);
    } else {
        assert(f->edits[0].span.start_line == a_l && f->edits[0].span.start_col == a_c1);
        assert(f->edits[0].span.end_line == a_l && f->edits[0].span.end_col == a_c2);
        assert(f->edits[1].span.start_line == u_l && f->edits[1].span.start_col == u_c1);
        assert(f->edits[1].span.end_line == u_l && f->edits[1].span.end_col == u_c2);
    }
    parser_state_destroy(&ps);
}

static void test_e0109_fix_positions(void) {
    /* "-> unit" is cols 8..14 of line 2: one edit deleting exactly that text */
    check_return_unit_fix("fn f() -> unit { }\n", 1, 2, 8, 10, 2, 11, 15);
    /* several blanks / a tab between: still one edit, still exactly arrow..unit */
    check_return_unit_fix("fn f() ->   unit { }\n", 1, 2, 8, 10, 2, 13, 17);
    check_return_unit_fix("fn f() ->\tunit { }\n", 1, 2, 8, 10, 2, 11, 15);
    /* a newline between: two edits, `->` and `unit` separately */
    check_return_unit_fix("fn f() ->\n    unit { }\n", 2, 2, 8, 10, 3, 5, 9);
    /* a block comment between: two edits, the comment is left alone */
    check_return_unit_fix("fn f() -> /* c */ unit { }\n", 2, 2, 8, 10, 2, 19, 23);
    /* a line comment + newline between */
    check_return_unit_fix("fn f() -> // c\n    unit { }\n", 2, 2, 8, 10, 3, 5, 9);
    printf("PASS: E0109 - fix-it deletes exactly \"-> unit\" (one edit), or the two tokens when a newline/comment sits between; the function stays in the tree\n");
}



/* ---- typed-IR step 2a: name spans -------------------------------------
 * The dump prints none of these, so goldens can't pin them; each is pinned
 * here to exact line:col (end column exclusive). A span that merely
 * "looks right" is the failure this guards against: the typed-IR stage
 * points its diagnostics (unknown function/attribute, naming violations,
 * duplicate names) at these, per principle 6. */
static void expect_span(const char *what, Span got, int l1, int c1, int l2, int c2) {
    if (got.start_line != l1 || got.start_col != c1 || got.end_line != l2 || got.end_col != c2) {
        fprintf(stderr, "FAIL: %s: got %d:%d-%d:%d, want %d:%d-%d:%d\n", what, got.start_line,
                got.start_col, got.end_line, got.end_col, l1, c1, l2, c2);
        exit(1);
    }
}

static NodeList parse_clean_file(ParserState *ps, const char *src) {
    parser_state_init(ps, 1 << 20);
    if (setjmp(ps->fatal_target) != 0) {
        fprintf(stderr, "FAIL: unexpected fatal longjmp\n");
        exit(1);
    }
    Lexer lx;
    lexer_init(&lx, src, strlen(src));
    NodeList items;
    bool ok = parser_parse_file(ps, &lx, &items);
    if (!ok || ps->diags.len != 0) {
        fprintf(stderr, "FAIL: name-span fixture did not parse cleanly (%zu diagnostics)\n", ps->diags.len);
        exit(1);
    }
    return items;
}

static void test_name_spans(void) {
    const char *src =
        "manna v0;\n"                      /* 1 */
        "struct Pt {\n"                    /* 2 */
        "    x: u32,\n"                    /* 3 */
        "}\n"                              /* 4 */
        "[kernel_hook]\n"                  /* 5 */
        "[a] fn go(p: u8) {\n"             /* 6 */
        "    let v = u64(p);\n"            /* 7 */
        "    let w = g(v).x;\n"            /* 8 */
        "    let z = (g(v)).x;\n"          /* 9 */
        "    let q = ( ( v ) );\n"         /* 10 */
        "}\n";                             /* 11 */
    ParserState ps;
    NodeList items = parse_clean_file(&ps, src);
    assert(items.len == 2);

    const Decl *st = items.items[0];
    expect_span("struct name", st->as.struct_decl.name_span, 2, 8, 2, 10);
    const Field *x = st->as.struct_decl.fields.items[0];
    expect_span("struct field name", x->name_span, 3, 5, 3, 6);
    expect_span("struct field whole span unchanged", x->span, 3, 5, 3, 12);

    const Decl *fn = items.items[1];
    assert(fn->as.function_decl.attributes.len == 2);
    const Attribute *a0 = fn->as.function_decl.attributes.items[0];
    const Attribute *a1 = fn->as.function_decl.attributes.items[1];
    assert(strcmp(a0->name, "kernel_hook") == 0 && strcmp(a1->name, "a") == 0);
    expect_span("attribute 0 span (brackets)", a0->span, 5, 1, 5, 14);
    expect_span("attribute 0 name_span", a0->name_span, 5, 2, 5, 13);
    expect_span("attribute 1 span (brackets)", a1->span, 6, 1, 6, 4);
    expect_span("attribute 1 name_span", a1->name_span, 6, 2, 6, 3);
    expect_span("function name", fn->as.function_decl.name_span, 6, 8, 6, 10);
    expect_span("function whole span starts at first attribute", (Span){fn->span.start_line, fn->span.start_col, 5, 14}, 5, 1, 5, 14);
    const Param *prm = fn->as.function_decl.params.items[0];
    expect_span("parameter name", prm->name_span, 6, 11, 6, 12);

    const Stmt *body = fn->as.function_decl.body;
    const Stmt *let_v = body->as.block.stmts.items[0];
    const Stmt *let_w = body->as.block.stmts.items[1];
    const Stmt *let_z = body->as.block.stmts.items[2];
    expect_span("let name", let_v->as.let_stmt.name_span, 7, 9, 7, 10);
    const Expr *conv = let_v->as.let_stmt.init;
    assert(conv->kind == EXPR_CONVERT);
    expect_span("conversion target keyword", conv->as.convert.target_span, 7, 13, 7, 16);

    const Expr *fa = let_w->as.let_stmt.init;
    assert(fa->kind == EXPR_FIELD_ACCESS);
    expect_span("field-access field name", fa->as.field_access.name_span, 8, 18, 8, 19);
    const Expr *call = fa->as.field_access.base;
    assert(call->kind == EXPR_CALL);
    expect_span("callee name", call->as.call.name_span, 8, 13, 8, 14);
    expect_span("call whole span", call->span, 8, 13, 8, 17);

    /* A parenthesized call: parse_primary widens the call's span to include
     * the parens, so the callee does NOT start at span.start any more. The
     * name_span must still be the identifier alone. */
    const Expr *fa2 = let_z->as.let_stmt.init;
    const Expr *call2 = fa2->as.field_access.base;
    assert(call2->kind == EXPR_CALL && call2->parenthesized);
    expect_span("parenthesized call: span widened to the parens", call2->span, 9, 13, 9, 19);
    expect_span("parenthesized call: callee name_span is the identifier", call2->as.call.name_span, 9, 14, 9, 15);
    expect_span("field name after a parenthesized base", fa2->as.field_access.name_span, 9, 20, 9, 21);
    /* A parenthesized identifier (step 18, re-audit finding F2): span covers every paren and space, name_span is the token alone. */
    const Stmt *let_q = body->as.block.stmts.items[3];
    const Expr *id = let_q->as.let_stmt.init;
    assert(id->kind == EXPR_IDENT && id->parenthesized);
    expect_span("parenthesized identifier: span widened to the parens", id->span, 10, 13, 10, 22);
    expect_span("parenthesized identifier: name_span is the identifier alone", id->as.ident.name_span, 10, 17, 10, 18);
    parser_state_destroy(&ps);

    /* A multi-line attribute: span covers both brackets across lines. */
    const char *src2 = "manna v0;\n[\n  kernel_hook\n]\nfn f() {}\n";
    ParserState ps2;
    NodeList items2 = parse_clean_file(&ps2, src2);
    const Decl *f2 = items2.items[0];
    const Attribute *at = f2->as.function_decl.attributes.items[0];
    expect_span("multi-line attribute span", at->span, 2, 1, 4, 2);
    expect_span("multi-line attribute name_span", at->name_span, 3, 3, 3, 14);
    expect_span("function name after a multi-line attribute", f2->as.function_decl.name_span, 5, 4, 5, 5);
    parser_state_destroy(&ps2);
    printf("PASS: name spans - every new span field pinned to exact line:col\n");
}

/* ---- typed-IR step 2b: the tree-height bound (E0111) -------------------
 * The FAIL side of the boundary is goldens (tests/parser/height_*_fail: 513
 * and far beyond). The ACCEPTED side lives here instead of as goldens: the
 * dump of a height-512 tree is ~N^2 bytes (indentation per level), ~1 MB
 * each, which does not belong in the repo. Each case builds a tree at the
 * largest height its shape can reach under MAX_EXPR_HEIGHT, asserts the
 * parse is clean and the root height is exactly that, then walks it with the
 * real AST printer into /dev/null - the walker the bound exists to protect.
 * The Makefile runs this test under `ulimit -s 8192`, so "survives" means
 * "survives an 8 MB stack", including in the ASan build. */
static unsigned h_max(unsigned a, unsigned b) { return a > b ? a : b; }
static unsigned eh(const Expr *e) { return e ? e->height : 0; }
static unsigned sh(const Stmt *s) {
    if (!s) return 0;
    unsigned m = 0;
    switch (s->kind) {
        case STMT_LET: return eh(s->as.let_stmt.init);
        case STMT_IF: return h_max(eh(s->as.if_stmt.cond), h_max(sh(s->as.if_stmt.then_branch), sh(s->as.if_stmt.else_branch)));
        case STMT_BLOCK:
            for (size_t i = 0; i < s->as.block.stmts.len; i++) m = h_max(m, sh(s->as.block.stmts.items[i]));
            return m;
        default: return 0;
    }
}

static char *rep_str(const char *unit, size_t n) {
    size_t u = strlen(unit);
    char *out = malloc(u * n + 1);
    for (size_t i = 0; i < n; i++) memcpy(out + i * u, unit, u);
    out[u * n] = '\0';
    return out;
}

static char *cat3(const char *a, const char *b, const char *c) {
    char *out = malloc(strlen(a) + strlen(b) + strlen(c) + 1);
    strcpy(out, a); strcat(out, b); strcat(out, c);
    return out;
}

static void check_accepted_at(const char *what, const char *src, unsigned want_height) {
    ParserState ps;
    NodeList items = parse_clean_file(&ps, src);
    const Decl *fn = items.items[0];
    unsigned got = sh(fn->as.function_decl.body);
    if (got != want_height) {
        fprintf(stderr, "FAIL: %s: root height %u, want %u\n", what, got, want_height);
        exit(1);
    }
    FILE *sink = fopen("/dev/null", "w");
    assert(sink);
    for (size_t i = 0; i < items.len; i++) decl_print(sink, items.items[i]); /* must not overflow the stack */
    fclose(sink);
    parser_state_destroy(&ps);
}

static void test_height_bound_accepted_side(void) {
    assert(MAX_EXPR_HEIGHT == 512); /* the numbers below are relative to it */
    const char *pre = "manna v0;\nfn f() { let x = ";
    const char *post = "; }\n";
    char *t, *src;

    {   char *e = cat3("1", (t = rep_str("+1", 511)), ""); free(t);
        src = cat3(pre, e, post); free(e);
        check_accepted_at("binary chain, height 512", src, 512); free(src); }
    {   char *e = cat3("a", (t = rep_str("+b*c", 510)), ""); free(t);
        src = cat3(pre, e, post); free(e);
        check_accepted_at("mixed-precedence chain, height 512", src, 512); free(src); }
    {   char *e = cat3("a", (t = rep_str(".f", 511)), ""); free(t);
        src = cat3(pre, e, post); free(e);
        check_accepted_at("field chain, height 512", src, 512); free(src); }
    {   char *e = cat3("a", (t = rep_str("[0]", 511)), ""); free(t);
        src = cat3(pre, e, post); free(e);
        check_accepted_at("index chain, height 512", src, 512); free(src); }
    {   /* two nodes per repetition, so the largest reachable height is odd: 511 */
        char *e = cat3("a", (t = rep_str(".f[0]", 255)), ""); free(t);
        src = cat3(pre, e, post); free(e);
        check_accepted_at("mixed postfix chain, height 511 (the largest odd height)", src, 511); free(src); }
    {   char *open = rep_str("f(", 200), *close = rep_str(")", 200), *adds = rep_str("+1", 311);
        char *inner = cat3("1", adds, ""); char *mid = cat3(open, inner, close);
        src = cat3(pre, mid, post);
        check_accepted_at("chain inside 200 nested calls, height 512 (total height counts, not just the chain)", src, 512);
        free(open); free(close); free(adds); free(inner); free(mid); free(src); }
    {   /* statement nesting adds nothing: 120 nested ifs around a height-512 chain */
        char *ifs = rep_str("if a {\n", 120), *closes = rep_str("}\n", 120), *adds = rep_str("+1", 511);
        size_t n = strlen("manna v0;\nfn f() {\n") + strlen(ifs) + strlen("let x = 1;\n") + strlen(adds) + strlen(closes) + 4;
        char *buf = malloc(n + 16);
        snprintf(buf, n + 16, "manna v0;\nfn f() {\n%slet x = 1%s;\n%s}\n", ifs, adds, closes);
        check_accepted_at("chain inside 120 nested ifs, height 512", buf, 512);
        free(ifs); free(closes); free(adds); free(buf); }
    printf("PASS: E0111 - a tree of height %d is accepted in every shape and the real printer walks it (8 MB stack)\n", MAX_EXPR_HEIGHT);
}

static void test_height_bound_fatal_and_reuse(void) {
    /* One past the bound: exactly one diagnostic, E0111, severity error, no
     * fix and no expected/found; the file is abandoned (fatal, like E0103);
     * and the ParserState parses the next valid file cleanly (per-parse
     * state resets, the R6 lesson). */
    static ParserState ps;
    parser_state_init(&ps, 1 << 22);
    char *chain = rep_str("+1", 512);
    char *e = cat3("1", chain, ""); free(chain);
    char *bad = cat3("manna v0;\nfn f() { let x = ", e, "; }\n"); free(e);
    NodeList items;
    Lexer lx;
    if (setjmp(ps.fatal_target) == 0) {
        lexer_init(&lx, bad, strlen(bad));
        parser_parse_file(&ps, &lx, &items);
        fprintf(stderr, "FAIL: a height-513 tree did not trigger the fatal escape\n");
        exit(1);
    }
    assert(ps.diags.len == 1);
    const Diagnostic *d = &ps.diags.items[0];
    assert(strcmp(d->code, "E0111") == 0 && d->severity == DIAG_ERROR);
    assert(d->fix == NULL && d->expected == NULL && d->found == NULL);
    assert(strstr(d->message, "more than 512 chained operations") != NULL);
    assert(d->span.start_line == 2 && d->span.start_col == 1043);

    const char *good = "manna v0;\nfn g() { let y = 1 + 2; }\n";
    lexer_init(&lx, good, strlen(good));
    if (setjmp(ps.fatal_target) != 0) { fprintf(stderr, "FAIL: valid file after E0111 hit a fatal\n"); exit(1); }
    assert(parser_parse_file(&ps, &lx, &items));
    /* diags is the caller-owned accumulated list and is deliberately not
     * cleared between parses (parser_state.h): the valid file must add NOTHING
     * to it, i.e. the one E0111 from the first parse is still the only entry. */
    assert(ps.diags.len == 1);
    assert(strcmp(ps.diags.items[0].code, "E0111") == 0);
    assert(items.len == 1);
    parser_state_destroy(&ps);
    free(bad);
    printf("PASS: E0111 - fatal with exactly one diagnostic (no fix), position pinned, state reusable afterwards\n");
}


/* Every guard_height() site in parser.c must be live: a removed guard would
 * silently let that construct build an over-tall tree, and the shape goldens
 * only exercise some sites. Each case wraps a chain C of height exactly 512
 * (the bound) in ONE more node of the construct under test, so that node is
 * the first to cross it: E0111 is expected, at the pinned column (the token
 * after the offending node completes - or, for a named argument, the ')' that
 * follows it, which is what tells its guard apart from the enclosing call's).
 * The control wraps a chain one shorter, so the wrapper is at height 512 and
 * the file parses clean. */
typedef struct { const char *name; const char *before; const char *after; int extra_for_control; } GuardCase;

static void test_height_guard_every_site(void) {
    /* before + "(" + C + ")" + after is the expression. */
    static const GuardCase cases[] = {
        {"binary ||",        "",       " || 1",      0},
        {"binary &&",        "",       " && 1",      0},
        {"relational <",     "",       " < 1",       0},
        {"binary |",         "",       " | 1",       0},
        {"binary ^",         "",       " ^ 1",       0},
        {"binary &",         "",       " & 1",       0},
        {"shift <<",         "",       " << 1",      0},
        {"additive +",       "",       " + 1",       0},
        {"multiplicative *", "",       " * 1",       0},
        {"unary -",          "-",      "",           0},
        {"named argument",   "f(a: ",  ")",          1}, /* the call above it adds a level too */
        {"call",             "f(",     ")",          0},
        {"array repeat",     "[",      "; 3]",       0},
        {"array literal",    "[",      ", 1]",       0},
        {"conversion",       "u8(",    ")",          0},
        {"field access",     "",       ".f",         0},
        {"index",            "a[",     "]",          0},
        {"slice",            "a[",     "..1]",       0},
    };
    const char *pre = "manna v0;\nfn f() { let x = ";
    const int expr_col = (int)strlen("fn f() { let x = ") + 1; /* column where the expression starts */
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const GuardCase *c = &cases[i];
        for (int control = 0; control < 2; control++) {
            /* C = 1 + k x "+1" has height k + 1; fail: 512 (k = 511); control: one less per level the
             * wrapper chain adds (named arg: 2 levels, so 510 -> 509 adds). */
            int k = control ? 510 - c->extra_for_control : 511;
            char *adds = rep_str("+1", (size_t)k);
            size_t n = strlen(pre) + strlen(c->before) + strlen(adds) + strlen(c->after) + 64;
            char *src = malloc(n);
            snprintf(src, n, "%s%s(1%s)%s; }\n", pre, c->before, adds, c->after);
            /* static: ps is modified after setjmp (the parse) and read again after
             * the longjmp (diags), which C11 7.13.2.1 makes indeterminate for an
             * automatic non-volatile object; same treatment as parser_main.c and
             * test_height_bound_fatal_and_reuse above. */
            static ParserState ps;
            parser_state_init(&ps, 1 << 22);
            Lexer lx;
            NodeList items;
            bool fatal = false;
            if (setjmp(ps.fatal_target) != 0) {
                fatal = true;
            } else {
                lexer_init(&lx, src, strlen(src));
                parser_parse_file(&ps, &lx, &items);
            }
            if (control) {
                if (fatal || ps.diags.len != 0) {
                    fprintf(stderr, "FAIL: guard control (%s): a tree at height %d was rejected\n", c->name, MAX_EXPR_HEIGHT);
                    exit(1);
                }
            } else {
                int want_col = expr_col + (int)(strlen(c->before) + 2 + 1 + strlen(adds) + strlen(c->after));
                if (c->extra_for_control) want_col -= 1; /* named arg: the ')' that closes the call */
                if (!fatal || ps.diags.len != 1 || strcmp(ps.diags.items[0].code, "E0111") != 0 ||
                    ps.diags.items[0].span.start_line != 2 || ps.diags.items[0].span.start_col != want_col) {
                    fprintf(stderr, "FAIL: guard site (%s): want one E0111 at 2:%d, got %zu diag(s)%s", c->name, want_col,
                            ps.diags.len, ps.diags.len ? "" : "\n");
                    if (ps.diags.len) fprintf(stderr, " first %s at %d:%d\n", ps.diags.items[0].code,
                                              ps.diags.items[0].span.start_line, ps.diags.items[0].span.start_col);
                    exit(1);
                }
            }
            parser_state_destroy(&ps);
            free(adds);
            free(src);
        }
    }
    printf("PASS: E0111 - every guard site (9 binary levels, unary, named arg, call, repeat, array, conversion, field, index, slice) trips at the bound, at its pinned column, and passes one level lower\n");
}
int main(void) {
    test_token_span_single_line_unchanged();
    test_token_span_multiline_unterminated_comment();
    test_token_span_multiline_trailing_text();
    test_parenthesized_flag_direct_literal();
    test_parenthesized_flag_wrapped_literal();
    test_version_decl_lexer_error_not_e0107();
    test_state_reset_after_fatal_longjmp();
    test_state_reset_error_count_between_parses();
    test_sync_counters_reset_at_parse_entry();
    test_unclosed_block_report_resets_between_parses();
    test_arena_exhaustion_reports_e0110();
    test_e0110_rearmed_on_state_reuse();
    test_harness_mode_routing();
    test_e0102_expected_field_per_site();
    test_e0108_fix_positions();
    test_e0109_fix_positions();
    test_name_spans();
    test_height_bound_accepted_side();
    test_height_bound_fatal_and_reuse();
    test_height_guard_every_site();
    printf("ALL PASS\n");
    return 0;
}
