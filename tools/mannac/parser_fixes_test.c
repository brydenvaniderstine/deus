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

    ParserState ps;
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

    ParserState ps;
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

static void check_e0110(const char *src, size_t len, size_t arena_size, const char *what) {
    ParserState ps;
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
    assert(d->span.start_line >= 1 && d->span.start_col >= 1);
    parser_state_destroy(&ps);
}

static void test_arena_exhaustion_reports_e0110(void) {
    size_t len;
    char *big_array = gen_list_input("manna v0;\nfn f() {\n    let a = ", "[", "];\n}\n", 300000, &len);
    check_e0110(big_array, len, 16 * 1024 * 1024, "300,000-element array literal");
    free(big_array);
    char *big_call = gen_list_input("manna v0;\nfn f() {\n    g(", "", ");\n}\n", 300000, &len);
    check_e0110(big_call, len, 16 * 1024 * 1024, "flat 300,000-argument call");
    free(big_call);
    char *small = gen_list_input("manna v0;\nfn f() {\n    let a = ", "[", "];\n}\n", 20000, &len);
    check_e0110(small, len, 256 * 1024, "20,000-element array against a 256 KiB arena");
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
    test_harness_mode_routing();
    test_e0102_expected_field_per_site();
    test_e0108_fix_positions();
    test_e0109_fix_positions();
    printf("ALL PASS\n");
    return 0;
}
