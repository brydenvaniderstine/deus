#include <stdbool.h>
#include <string.h>
#include "parser.h"

/* ---------------------------------------------------------------- *
 * Token-stream plumbing: one token of lookahead (LL(1), per the grammar),
 * primed before the first parse call and advanced explicitly.
 * ---------------------------------------------------------------- */

typedef struct {
    ParserState *ps;
    Lexer       *lx;
    Token        cur;
} Parser;

static void advance(Parser *p) {
    p->cur = lexer_next(p->lx);
}

/* Exposed (not static) so parser_fixes_test.c can verify the multi-line
 * case directly, without pulling in the rest of the parser's machinery
 * just to exercise one pure function - see parser.h. */
Span token_span(Token t) {
    Span s;
    s.start_line = t.line;
    s.start_col = t.col;

    /* Every token kind except TOK_ERROR_UNTERMINATED_COMMENT is known to
     * lex from exactly one line (comments are stripped transparently by
     * the lexer and never reach the parser as tokens at all when closed
     * properly - see lexer.c's skip_ws_and_comments). An unterminated
     * block comment is the one case where `lexeme` can contain embedded
     * '\n's, spanning from its opening delimiter all the way to EOF - the
     * simple "end_col = col + lexeme_len" formula below silently produces
     * a nonsensical end position for it (adding a byte count that mostly
     * belongs to OTHER lines onto a single line's column). Scanning for
     * embedded newlines here, rather than special-casing the one token
     * kind that can have them, keeps this correct automatically for any
     * future multi-line token kind too. */
    int newlines = 0;
    size_t last_newline_offset = 0;
    for (size_t i = 0; i < t.lexeme_len; i++) {
        if (t.lexeme[i] == '\n') {
            newlines++;
            last_newline_offset = i;
        }
    }
    if (newlines > 0) {
        s.end_line = t.line + newlines;
        /* 1-based column right after the last character on the final
         * line this token covers - exclusive-end, same convention as the
         * single-line formula below (there, "col + lexeme_len" is simply
         * this same idea when last_newline_offset is 0 and t.col is
         * folded in; here the line restarts at column 1 after the '\n',
         * so t.col plays no part in the end column at all). */
        s.end_col = (int)(t.lexeme_len - last_newline_offset);
    } else {
        s.end_line = t.line;
        s.end_col = t.col + (int)t.lexeme_len;
    }
    return s;
}

static Span span_merge(Span start, Span end) {
    Span s;
    s.start_line = start.start_line;
    s.start_col = start.start_col;
    s.end_line = end.end_line;
    s.end_col = end.end_col;
    return s;
}

static bool is_lexer_error_token(TokenKind k) {
    return k == TOK_ERROR_BAD_CHAR || k == TOK_ERROR_UNTERMINATED_COMMENT ||
           k == TOK_ERROR_MALFORMED_NUMBER || k == TOK_ERROR_NUMBER_OVERFLOW;
}

/* Short description for a diagnostic's "found" slot. Error tokens use the
 * lexer's own message (more useful than quoting whatever garbage text
 * triggered it); everything else quotes its literal source text. Bridging
 * lexer errors to their own E00xx codes is deferred alongside the rest of
 * that registry (docs/error_codes.md) - here, an error token is simply
 * "the wrong thing found in this position", same as any other. */
static const char *token_describe(Arena *a, Token t) {
    if (t.kind == TOK_EOF) {
        return "end of file";
    }
    if (is_lexer_error_token(t.kind)) {
        return t.error_msg;
    }
    return arena_sprintf(a, "'%.*s'", (int)t.lexeme_len, t.lexeme);
}

/* ---------------------------------------------------------------- *
 * Grammar (docs/M2_PLAN.md S4):
 *
 *   expression     ::= logic_or ;
 *   logic_or       ::= logic_and { '||' logic_and } ;
 *   logic_and      ::= relational { '&&' relational } ;
 *   relational     ::= bitor [ rel_op bitor ] ;   (one comparison/equality op per operand)
 *   rel_op         ::= '<' | '>' | '<=' | '>=' | '==' | '!=' ;
 *   bitor          ::= bitxor { '|' bitxor } ;
 *   bitxor         ::= bitand { '^' bitand } ;
 *   bitand         ::= shift { '&' shift } ;
 *   shift          ::= additive { ('<<'|'>>') additive } ;
 *   additive       ::= multiplicative { ('+'|'-') multiplicative } ;
 *   multiplicative ::= unary { ('*'|'/'|'%') unary } ;
 *   unary          ::= ('!'|'-'|'~') unary | postfix ;
 *   postfix        ::= primary { '.' IDENT | '[' index_or_slice ']' } ;
 *   index_or_slice ::= expression [ '..' expression ] ;
 *   primary        ::= NUMBER | KW_TRUE | KW_FALSE
 *                     | IDENT [ '(' [ arg_list ] ')' ]
 *                     | PRIM_TYPE '(' expression ')'
 *                     | '[' array_body ']'
 *                     | '(' expression ')'
 *                     ;
 *   arg_list       ::= arg { ',' arg } ;
 *   arg            ::= expression [ ':' expression ] ;
 *   array_body     ::= expression ( { ',' expression } | ';' NUMBER ) ;
 *
 *   block          ::= '{' { statement } '}' ;
 *   statement      ::= let_stmt | if_stmt | while_stmt | for_stmt
 *                     | return_stmt | break_stmt | continue_stmt
 *                     | postfix_stmt | expr_stmt ;
 *   let_stmt       ::= 'let' IDENT [ ':' type ] '=' expression ';' ;
 *   postfix_stmt   ::= postfix expr_tail ';' ;
 *   expr_tail      ::= '=' expression | (* empty, continue as expression *) ;
 *   if_stmt        ::= 'if' expression block [ 'else' ( if_stmt | block ) ] ;
 *   while_stmt     ::= 'while' expression block ;
 *   for_stmt       ::= 'for' ( let_stmt | postfix_stmt | expr_stmt )
 *                      expression ';' for_step block ;
 *   for_step       ::= postfix expr_tail ;
 *   break_stmt     ::= 'break' ';' ;
 *   continue_stmt  ::= 'continue' ';' ;
 *   return_stmt    ::= 'return' [ expression ] ';' ;
 *   expr_stmt      ::= expression ';' ;
 *   type           ::= PRIM_TYPE | IDENT | '[' INTEGER ']' type | '[' ']' type ;
 *   PRIM_TYPE      ::= 'u8' | 'u16' | 'u32' | 'u64' | 'i8' | 'i16' | 'i32'
 *                    | 'i64' | 'bool' ;   (keywords; `unit` is reserved but NOT here)
 *
 *   translation_unit  ::= version_decl { top_level_item } ;
 *   version_decl      ::= 'manna' IDENT ';' ;
 *   top_level_item    ::= struct_decl | function_decl ;
 *   attribute         ::= '[' IDENT ']' ;
 *   struct_decl       ::= 'struct' IDENT '{' { field_decl } '}' ;
 *   field_decl        ::= IDENT ':' type ',' ;
 *   function_decl     ::= { attribute } 'fn' IDENT '(' [ param_list ] ')'
 *                         [ '->' type ] block ;
 *   param_list        ::= param { ',' param } ;
 *   param             ::= IDENT ':' type ;
 *
 * postfix_stmt/expr_stmt are disjoint by construction (starts_postfix),
 * not by a FIRST-set table: anything that can start a `postfix` tries
 * postfix_stmt; everything else (a bare unary prefix) tries expr_stmt.
 * Within postfix_stmt/for_step, the real decision - assignment vs. a
 * plain expression statement - needs exactly one token of lookahead
 * right after the shared `postfix` prefix ('=' or not), not before it;
 * seeing this as two separate `statement` alternatives (the way an
 * earlier draft of this grammar had it) would have an unbounded-lookahead
 * FIRST-set overlap. See parse_expr_tail / parse_postfix_expr_tail.
 *
 * Depth bound: only parse_expression, parse_unary's self-recursive prefix
 * case, parse_type (self-recursive: array/slice element types nest),
 * parse_block, and parse_if_stmt (self-recursive: the else-if chain)
 * carry depth_enter/depth_exit - not the 9 fixed-pipeline binary-
 * operator levels between logic_or and multiplicative (parsing a plain
 * "a" always walks all 10, a constant cost independent of input, so
 * instrumenting them individually would only inflate the counter without
 * bounding anything new), not primary/postfix/arg_list/index_or_slice
 * either (an earlier version of this file put hooks there too - removed
 * once proven redundant and worse: it cost extra depth per level on top
 * of parse_expression's own hook, capping real nesting around 85 levels
 * instead of the intended 256; see that commit message and tests/parser/
 * depth_*), and not parse_statement, parse_postfix_or_expr_stmt,
 * parse_for_init, parse_while_stmt, or parse_for_stmt either (one-shot
 * dispatches or wrappers, not cycles of their own - same character as
 * the 10 pipeline levels).
 *
 * INVARIANT: every recursive cycle in this call graph passes through at
 * least one depth-counted function, so no cycle can spin arbitrarily deep
 * without the shared counter incrementing. The call graph is a strict DAG
 * (expression -> logic_or -> ... -> multiplicative -> unary -> postfix ->
 * primary; separately, block -> statement -> {every statement kind},
 * with let_stmt also reaching type, and if/while/for_stmt each reaching
 * block again) plus exactly nine back-edges, each one a cycle:
 *
 *   1. unary -> unary (self-recursive prefix case, "!!!!x")
 *      Counted by: parse_unary's own hook.
 *   2. primary -> expression (the '(' expression ')' case, "(((x)))")
 *      Counted by: parse_expression's hook.
 *   3. primary -> arg_list -> expression (call args, "f(f(f(x)))", and
 *      both sides of a named arg, "f(a: f(a: x))" - #4 step 4 extends this
 *      cycle, it adds none)
 *      Counted by: parse_expression's hook.
 *   4. postfix -> index_or_slice -> expression (index/slice bounds,
 *      "a[a[a[x]]]")
 *      Counted by: parse_expression's hook.
 *   5. type -> type (self-recursive array/slice element, "[1][1][1]...T")
 *      Counted by: parse_type's own hook.
 *   6. {if,while,for}_stmt -> block -> statement -> {if,while,for}_stmt
 *      (real nesting, "if a { if b { ... } }")
 *      Counted by: parse_block's own hook (and, for the if/if case,
 *      redundantly by parse_if_stmt's too - both are "open" at once).
 *   7. if_stmt -> if_stmt (the "else if" chain, "if a {} else if b {}
 *      else if c {} ...") - does NOT pass through parse_block at all;
 *      an "else if" link has no block of its own, only a bare
 *      "else { }" does.
 *      Counted by: parse_if_stmt's own hook - this is exactly why it
 *      needs one, the same reasoning as parse_unary and parse_type.
 *   8. primary -> expression (the PRIM_TYPE '(' expression ')' conversion,
 *      "u8(u8(u8(x)))") - added in #4 step 2; "cycle 10" in docs/M2_PLAN.md's
 *      depth table, whose numbering counts that table's rows, not this list.
 *      Counted by: parse_expression's hook, same as cycles 2-4.
 *   9. primary -> array_body -> expression (array literal elements and the
 *      repeat literal's value, "[[[x]]]", "[[[x; 1]; 1]; 1]") - added in #4
 *      step 3; "cycle 9" in docs/M2_PLAN.md's depth table (that numbering
 *      counts the table's rows, so this list's items 8 and 9 are the
 *      table's cycles 10 and 9). The repeat count is a NUMBER leaf.
 *      Counted by: parse_expression's hook, same as cycles 2-4.
 *
 * Error recovery realizes cycles 6 and 7 a second way, and is NOT an
 * eighth cycle: handle_header_failure -> parse_block (the recovered
 * body), and, since the R1 fix, parse_if_stmt -> handle_header_failure ->
 * recover_else_branch -> parse_if_stmt / parse_block (the recovered else
 * chain). Each passes through the same counted function as the direct
 * path (parse_block / parse_if_stmt - the latter hooks on entry, before
 * any recovery runs), so recovery adds no back-edge to the list above. Measured, not predicted:
 * a failed `if` header followed by a valid else-if chain caps at 254
 * total `if`s (255 and 300 fail with E0103), identical to the direct
 * chain - tests/parser/depth_elseif_recovered_*.
 *
 * Step 8 (translation_unit/top_level_item/struct_decl/function_decl)
 * introduces no eighth cycle: top_level_item is a flat dispatch over a
 * non-recursive loop in parser_parse_file (nothing calls translation_unit
 * or top_level_item again), struct_decl's fields don't recurse into
 * top_level_item, and function_decl's only recursive content is its body
 * (a block, cycle 6 already) and its params'/return type (type, cycle 5
 * already). Nothing new to hook, nothing new to empirically prove - this
 * is itself the finding, confirmed by inspecting the call graph rather
 * than assumed because nothing obviously recurses.
 *
 * Cycles 1-5 were proven at the cap in Steps 5-6 (tests/parser/depth_*);
 * cycle 6 (true if/while/for nesting) and cycle 7 (else-if chains) are
 * proven here (tests/parser/depth_nested_if_*, depth_elseif_*) - both
 * cap below the single-construct 255/256 figures from Steps 5-6, since
 * each level here costs more than 1 depth unit (block's hook, and for
 * cycle 6 also if_stmt's), the same "shared budget" effect Step 6 found
 * for nested array types inside a block. docs/M2_PLAN.md's "Depth-bound
 * proof summary" table (Step 9) consolidates every cycle, counted
 * function, and proven limit in one place - that table, not a restatement
 * here, is the place to look for the full picture across all of them.
 *
 * Any FUTURE recursive parse function must preserve this invariant:
 * either it sits on a cycle that already passes through parse_expression
 * or parse_block (nothing extra needed), or it introduces a new kind of
 * cycle that doesn't, in which case IT needs its own depth_enter/
 * depth_exit, the same way parse_unary, parse_type, and parse_if_stmt do
 * for their self-recursion. The thing to check before adding a new
 * recursive production is not "does every function have a hook" but
 * "does every cycle have at least one."
 *
 * On a failure that isn't locally recoverable, the function that hit it
 * records a diagnostic (parser_error - itself possibly fatal if this is
 * the MAX_ERRORS'th one) and returns NULL; every caller here checks for
 * NULL and propagates it immediately rather than attempting to continue -
 * all the way up to the nearest level with a resume point of its own
 * (statement, block, header, or top-level item), where Step 10b's
 * Wirth-style follow-set recovery (see the sync_* functions, and
 * docs/M2_PLAN.md's "Error recovery" table) takes over: the bad
 * statement/item is dropped and parsing resumes at the next
 * synchronization point, rather than the whole enclosing block/file
 * aborting. The binary-operator
 * levels below need no diagnostic calls of their own: a failed operand
 * eventually bottoms out at parse_primary, which already emits "expected
 * expression, found X" - every level above it just propagates NULL.
 * ---------------------------------------------------------------- */

static Expr *parse_expression(Parser *p);
static Expr *parse_logic_or(Parser *p, Expr *given_lhs);
static Expr *parse_logic_and(Parser *p, Expr *given_lhs);
static Expr *parse_relational(Parser *p, Expr *given_lhs);
static Expr *parse_bitor(Parser *p, Expr *given_lhs);
static Expr *parse_bitxor(Parser *p, Expr *given_lhs);
static Expr *parse_bitand(Parser *p, Expr *given_lhs);
static Expr *parse_shift(Parser *p, Expr *given_lhs);
static Expr *parse_additive(Parser *p, Expr *given_lhs);
static Expr *parse_multiplicative(Parser *p, Expr *given_lhs);
static Expr *parse_unary(Parser *p);
static Expr *parse_postfix(Parser *p);
static Expr *parse_primary(Parser *p);
static bool  parse_arg_list(Parser *p, NodeList *out_args);
static bool  parse_index_or_slice(Parser *p, Expr **out_lo, Expr **out_hi);
static Type *parse_type(Parser *p);
static bool  is_prim_type_keyword(TokenKind k);
static Stmt *parse_statement(Parser *p);
static Stmt *parse_block(Parser *p);
static Stmt *parse_let_stmt(Parser *p);
static Stmt *parse_return_stmt(Parser *p);
static Stmt *parse_break_stmt(Parser *p);
static Stmt *parse_continue_stmt(Parser *p);
static Expr *parse_expr_tail(Parser *p, Expr *lhs);
static bool  starts_postfix(TokenKind k);
static Stmt *parse_postfix_expr_tail(Parser *p, bool require_semi);
static Stmt *parse_postfix_stmt(Parser *p);
static Stmt *parse_expr_stmt(Parser *p);
static Stmt *parse_postfix_or_expr_stmt(Parser *p);
static Stmt *parse_if_stmt(Parser *p);
static Stmt *parse_while_stmt(Parser *p);
static Stmt *parse_for_stmt(Parser *p);
static Stmt *parse_for_init(Parser *p);
static Stmt *parse_for_step(Parser *p);
static bool  parse_version_decl(Parser *p);
static Decl *parse_top_level_item(Parser *p);
static Decl *parse_struct_decl(Parser *p);
static Field *parse_field_decl(Parser *p);
static bool  parse_attribute(Parser *p, const char **out_name);
static Param *parse_param(Parser *p);
static bool  parse_param_list(Parser *p, NodeList *out_params);
static Decl *parse_function_decl(Parser *p);

static Expr *parse_expression(Parser *p) {
    depth_enter(p->ps, token_span(p->cur));
    Expr *e = parse_logic_or(p, NULL);
    depth_exit(p->ps);
    return e;
}

/* expr_tail ::= '=' expression | (* empty, continuing as a plain
 * expression *) ; - NOT called for the '=' case (postfix_stmt/for_step
 * check for TOK_ASSIGN themselves, since that case's RHS is a fresh
 * parse_expression, not a continuation). This function IS the "empty"
 * case: resume the same precedence-climbing chain one level up from
 * postfix (at multiplicative and above), with the already-parsed
 * postfix as the initial left-hand operand instead of parsing a fresh
 * one - exactly the mechanism M2_PLAN.md's expr_tail comment describes
 * operationally, not expressible as a flat CFG production. No depth hook
 * needed: this is a flat sequential chain through already-counted (or
 * deliberately uncounted pipeline) functions, not a cycle of its own. */
static Expr *parse_expr_tail(Parser *p, Expr *lhs) {
    lhs = parse_multiplicative(p, lhs); if (!lhs) return NULL;
    lhs = parse_additive(p, lhs);       if (!lhs) return NULL;
    lhs = parse_shift(p, lhs);          if (!lhs) return NULL;
    lhs = parse_bitand(p, lhs);         if (!lhs) return NULL;
    lhs = parse_bitxor(p, lhs);         if (!lhs) return NULL;
    lhs = parse_bitor(p, lhs);          if (!lhs) return NULL;
    lhs = parse_relational(p, lhs);     if (!lhs) return NULL;
    lhs = parse_logic_and(p, lhs);      if (!lhs) return NULL;
    lhs = parse_logic_or(p, lhs);       if (!lhs) return NULL;
    return lhs;
}

static Expr *parse_logic_or(Parser *p, Expr *given_lhs) {
    Expr *lhs = given_lhs ? given_lhs : parse_logic_and(p, NULL);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_OR) {
        advance(p);
        Expr *rhs = parse_logic_and(p, NULL);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), BINOP_OR, lhs, rhs);
    }
    return lhs;
}

static Expr *parse_logic_and(Parser *p, Expr *given_lhs) {
    Expr *lhs = given_lhs ? given_lhs : parse_relational(p, NULL);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_AND) {
        advance(p);
        Expr *rhs = parse_relational(p, NULL);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), BINOP_AND, lhs, rhs);
    }
    return lhs;
}

/* relational ::= bitor [ rel_op bitor ] ; - ONE level for all six of
 * < > <= >= == != (#4 step 5; it replaces the former two levels, equality
 * over comparison). Non-associative: at most ONE comparison/equality
 * operator per operand of && / || (every expression is such an operand).
 * After the first operator and its right operand, a second operator is an
 * error, and which one depends on the classes (comparison: < > <= >=;
 * equality: == !=):
 *   - same class  (`a < b < c`, `a == b != c`): E0101, no fix-it - the
 *     natural rewrite `a < b && b < c` evaluates `b` twice;
 *   - different   (`a < b == c`, `a == b < c`): E0108, with a fix-it that
 *     parenthesizes the comparison-class pair (the meaning the old two-level
 *     grammar gave it): two zero-width inserts, `(` before the pair's left
 *     operand and `)` after its right one. Only spans are needed, never
 *     source text. For `a == b < c` the pair is (b, c), so c is parsed
 *     before reporting just to learn where `)` goes.
 * One diagnostic per operand: the error returns NULL and statement-level
 * recovery takes over, exactly as for E0101. Parenthesized operands re-enter
 * `expression`, so `(a < b) == c` is two operands' worth of single operators
 * and valid; a parenthesized operand's span includes its parens (Phase A
 * #7), so the `(` insert lands before them. */
static bool is_comparison_op(TokenKind k) {
    return k == TOK_LT || k == TOK_GT || k == TOK_LE || k == TOK_GE;
}

static bool is_equality_op(TokenKind k) {
    return k == TOK_EQ || k == TOK_NE;
}

static bool is_comparison_or_equality_op(TokenKind k) {
    return is_comparison_op(k) || is_equality_op(k);
}

static BinOp relational_binop(TokenKind k) {
    switch (k) {
        case TOK_LT: return BINOP_LT;
        case TOK_GT: return BINOP_GT;
        case TOK_LE: return BINOP_LE;
        case TOK_GE: return BINOP_GE;
        case TOK_EQ: return BINOP_EQ;
        default:     return BINOP_NE; /* TOK_NE */
    }
}

static Expr *parse_relational(Parser *p, Expr *given_lhs) {
    Expr *lhs = given_lhs ? given_lhs : parse_bitor(p, NULL);
    if (!lhs) return NULL;
    if (!is_comparison_or_equality_op(p->cur.kind)) {
        return lhs;
    }
    TokenKind first_op = p->cur.kind;
    Span first_left = lhs->span;
    advance(p);
    Expr *rhs = parse_bitor(p, NULL);
    if (!rhs) return NULL;
    Span first_right = rhs->span;
    lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), relational_binop(first_op), lhs, rhs);

    if (is_comparison_or_equality_op(p->cur.kind)) {
        Token second = p->cur;
        if (is_equality_op(first_op) == is_equality_op(second.kind)) {
            parser_error(p->ps, diag_chained_comparison(token_span(second)));
            return NULL;
        }
        Span pair_left = first_left, pair_right = first_right; /* comparison op came first */
        if (is_equality_op(first_op)) {
            /* equality first, comparison second: the pair to parenthesize
             * is (first right operand, the comparison's right operand) */
            advance(p);
            Expr *third = parse_bitor(p, NULL);
            if (!third) return NULL;
            pair_left = first_right;
            pair_right = third->span;
        }
        parser_error(p->ps, diag_mixed_comparison(p->ps->arena, token_span(second), pair_left, pair_right));
        return NULL;
    }
    return lhs;
}

static Expr *parse_bitor(Parser *p, Expr *given_lhs) {
    Expr *lhs = given_lhs ? given_lhs : parse_bitxor(p, NULL);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_PIPE) {
        advance(p);
        Expr *rhs = parse_bitxor(p, NULL);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), BINOP_BITOR, lhs, rhs);
    }
    return lhs;
}

static Expr *parse_bitxor(Parser *p, Expr *given_lhs) {
    Expr *lhs = given_lhs ? given_lhs : parse_bitand(p, NULL);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_CARET) {
        advance(p);
        Expr *rhs = parse_bitand(p, NULL);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), BINOP_BITXOR, lhs, rhs);
    }
    return lhs;
}

static Expr *parse_bitand(Parser *p, Expr *given_lhs) {
    Expr *lhs = given_lhs ? given_lhs : parse_shift(p, NULL);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_AMP) {
        advance(p);
        Expr *rhs = parse_shift(p, NULL);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), BINOP_BITAND, lhs, rhs);
    }
    return lhs;
}

static Expr *parse_shift(Parser *p, Expr *given_lhs) {
    Expr *lhs = given_lhs ? given_lhs : parse_additive(p, NULL);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_SHL || p->cur.kind == TOK_SHR) {
        BinOp op = p->cur.kind == TOK_SHL ? BINOP_SHL : BINOP_SHR;
        advance(p);
        Expr *rhs = parse_additive(p, NULL);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), op, lhs, rhs);
    }
    return lhs;
}

static Expr *parse_additive(Parser *p, Expr *given_lhs) {
    Expr *lhs = given_lhs ? given_lhs : parse_multiplicative(p, NULL);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_PLUS || p->cur.kind == TOK_MINUS) {
        BinOp op = p->cur.kind == TOK_PLUS ? BINOP_ADD : BINOP_SUB;
        advance(p);
        Expr *rhs = parse_multiplicative(p, NULL);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), op, lhs, rhs);
    }
    return lhs;
}

static Expr *parse_multiplicative(Parser *p, Expr *given_lhs) {
    Expr *lhs = given_lhs ? given_lhs : parse_unary(p);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_STAR || p->cur.kind == TOK_SLASH || p->cur.kind == TOK_PERCENT) {
        BinOp op = p->cur.kind == TOK_STAR ? BINOP_MUL : p->cur.kind == TOK_SLASH ? BINOP_DIV : BINOP_MOD;
        advance(p);
        Expr *rhs = parse_unary(p);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), op, lhs, rhs);
    }
    return lhs;
}

static Expr *parse_unary(Parser *p) {
    UnOp op;
    switch (p->cur.kind) {
        case TOK_BANG:  op = UNOP_NOT;    break;
        case TOK_MINUS: op = UNOP_NEG;    break;
        case TOK_TILDE: op = UNOP_BITNOT; break;
        default:
            return parse_postfix(p);
    }
    depth_enter(p->ps, token_span(p->cur));
    Token op_tok = p->cur;
    advance(p);
    Expr *operand = parse_unary(p);
    if (!operand) {
        depth_exit(p->ps);
        return NULL;
    }
    Expr *result = expr_unary(p->ps->arena, span_merge(token_span(op_tok), operand->span), op, operand);
    depth_exit(p->ps);
    return result;
}

static bool parse_arg_list(Parser *p, NodeList *out_args) {
    node_list_init(out_args);
    for (;;) {
        Expr *arg = parse_expression(p);
        if (!arg) {
            return false;
        }
        /* arg ::= expression [ ':' expression ] - struct construction,
         * `Point(x: 1, y: 2)`. The left side is ANY expression here; that
         * it is a bare field name, that the callee is a struct, and that
         * named and positional args are not mixed are all IR-stage checks
         * (the parser has no symbol table). Both sides go through
         * parse_expression (counted) - cycle 3, extended, not a new cycle:
         * `f(a: f(a: ...))` costs one unit per level like `f(f(...))`. */
        if (p->cur.kind == TOK_COLON) {
            advance(p);
            Expr *value = parse_expression(p);
            if (!value) {
                return false;
            }
            arg = expr_named_arg(p->ps->arena, span_merge(arg->span, value->span), arg, value);
        }
        node_list_push(p->ps->arena, out_args, arg);
        if (p->cur.kind == TOK_COMMA) {
            advance(p);
            continue;
        }
        break;
    }
    return true;
}

static bool parse_index_or_slice(Parser *p, Expr **out_lo, Expr **out_hi) {
    Expr *lo = parse_expression(p);
    if (!lo) {
        return false;
    }
    Expr *hi = NULL;
    if (p->cur.kind == TOK_DOTDOT) {
        advance(p);
        hi = parse_expression(p);
        if (!hi) {
            return false;
        }
    }
    *out_lo = lo;
    *out_hi = hi;
    return true;
}

static Expr *parse_primary(Parser *p) {
    Expr *result = NULL;
    Token t = p->cur;

    switch (t.kind) {
        case TOK_NUMBER:
            advance(p);
            result = expr_number(p->ps->arena, token_span(t), t.number_value);
            break;

        case TOK_KW_TRUE:
            advance(p);
            result = expr_bool(p->ps->arena, token_span(t), true);
            break;

        case TOK_KW_FALSE:
            advance(p);
            result = expr_bool(p->ps->arena, token_span(t), false);
            break;

        case TOK_IDENT:
            advance(p);
            if (p->cur.kind == TOK_LPAREN) {
                advance(p); /* consume '(' */
                NodeList args;
                bool ok = true;
                if (p->cur.kind == TOK_RPAREN) {
                    node_list_init(&args);
                } else {
                    ok = parse_arg_list(p, &args);
                }
                if (ok) {
                    if (p->cur.kind == TOK_RPAREN) {
                        Token rparen = p->cur;
                        advance(p);
                        result = expr_call(p->ps->arena, span_merge(token_span(t), token_span(rparen)),
                                            t.lexeme, t.lexeme_len, args);
                    } else {
                        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "')'",
                                                           token_describe(p->ps->arena, p->cur), "after arguments"));
                    }
                }
            } else {
                result = expr_ident(p->ps->arena, token_span(t), t.lexeme, t.lexeme_len);
            }
            break;

        case TOK_LPAREN:
            advance(p); /* consume '(' */
            result = parse_expression(p);
            if (result) {
                if (p->cur.kind == TOK_RPAREN) {
                    /* No Paren AST node (the grammar deliberately doesn't
                     * have one - M2_PLAN.md's compact rendering), so the
                     * only place to record "this was parenthesized" is on
                     * the inner expression itself: widen its span to cover
                     * the parens too (an inner literal's span would
                     * otherwise under-report where the expression it's
                     * part of actually starts/ends), and set the flag the
                     * IR stage needs to tell "-9223372036854775808" (a
                     * literal directly negated - i64::MIN) apart from
                     * "-(9223372036854775808)" (the same magnitude, but
                     * through a parenthesized sub-expression - an overflow
                     * error instead, docs/M2_PLAN.md records why). Mutating
                     * in place is safe and correct even through nested
                     * parens, e.g. "((42))": each layer's RPAREN handling
                     * widens the span a little further and the flag is
                     * already true after the first, a no-op to set again. */
                    Token rparen = p->cur;
                    advance(p);
                    result->span = span_merge(token_span(t), token_span(rparen));
                    result->parenthesized = true;
                } else {
                    parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "')'",
                                                       token_describe(p->ps->arena, p->cur), "after expression"));
                    result = NULL;
                }
            }
            break;

        case TOK_LBRACKET: {
            /* '[' array_body ']' : array_body ::= expression
             * ( { ',' expression } | ';' NUMBER ). Both forms re-enter
             * parse_expression (counted) - depth cycle 9 in
             * docs/M2_PLAN.md's table. No trailing comma, and `[]` is an
             * error (no element to infer a type from): both fall out of
             * parse_expression failing on ']'. N in `[v; N]` is a NUMBER
             * token, not an expression (v0 has no constant evaluation), so
             * `[v; x]` and `[v; -1]` are errors; N == 0 and N == 1 are
             * legal here (the IR stage and the formatter own the rest). */
            advance(p); /* consume '[' */
            Expr *first = parse_expression(p);
            if (!first) {
                break;
            }
            if (p->cur.kind == TOK_SEMI) {
                advance(p);
                if (p->cur.kind != TOK_NUMBER) {
                    parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "array repeat count (integer)",
                                                       token_describe(p->ps->arena, p->cur), NULL));
                    break;
                }
                uint64_t count = p->cur.number_value;
                advance(p);
                if (p->cur.kind != TOK_RBRACKET) {
                    parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "']'",
                                                       token_describe(p->ps->arena, p->cur), "after repeat count"));
                    break;
                }
                Token rb = p->cur;
                advance(p);
                result = expr_array_repeat(p->ps->arena, span_merge(token_span(t), token_span(rb)), first, count);
                break;
            }
            NodeList elems;
            node_list_init(&elems);
            node_list_push(p->ps->arena, &elems, first);
            bool ok = true;
            while (p->cur.kind == TOK_COMMA) {
                advance(p);
                Expr *next = parse_expression(p);
                if (!next) {
                    ok = false;
                    break;
                }
                node_list_push(p->ps->arena, &elems, next);
            }
            if (!ok) {
                break;
            }
            if (p->cur.kind != TOK_RBRACKET) {
                parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "']'",
                                                   token_describe(p->ps->arena, p->cur), "after array elements"));
                break;
            }
            Token rb = p->cur;
            advance(p);
            result = expr_array(p->ps->arena, span_merge(token_span(t), token_span(rb)), elems);
            break;
        }

        case TOK_KW_U8: case TOK_KW_U16: case TOK_KW_U32: case TOK_KW_U64:
        case TOK_KW_I8: case TOK_KW_I16: case TOK_KW_I32: case TOK_KW_I64:
        case TOK_KW_BOOL: {
            /* PRIM_TYPE '(' expression ')' - explicit conversion, exactly one
             * operand. The keyword alone is not an expression, and (unlike
             * IDENT) it never takes an arg_list: `u8()` and `u8(a, b)` are
             * syntax errors here. Which conversions are legal (bool <-> int
             * is not) is an IR-stage decision, so `bool(x)` and `u8(true)`
             * parse. Re-enters parse_expression (counted) - depth cycle 10
             * in docs/M2_PLAN.md's table. */
            advance(p);
            if (p->cur.kind != TOK_LPAREN) {
                parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "'('",
                                                   token_describe(p->ps->arena, p->cur),
                                                   arena_sprintf(p->ps->arena, "after '%.*s'", (int)t.lexeme_len, t.lexeme)));
                break;
            }
            advance(p); /* consume '(' */
            Expr *operand = parse_expression(p);
            if (!operand) {
                break;
            }
            if (p->cur.kind != TOK_RPAREN) {
                parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "')'",
                                                   token_describe(p->ps->arena, p->cur), "after conversion operand"));
                break;
            }
            Token rparen = p->cur;
            advance(p);
            result = expr_convert(p->ps->arena, span_merge(token_span(t), token_span(rparen)),
                                  t.lexeme, t.lexeme_len, operand);
            break;
        }

        default:
            parser_error(p->ps, diag_expected(p->ps->arena, token_span(t), "expression",
                                               token_describe(p->ps->arena, t), NULL));
            break;
    }

    return result;
}

static Expr *parse_postfix(Parser *p) {
    Expr *base = parse_primary(p);
    if (!base) {
        return NULL;
    }

    for (;;) {
        if (p->cur.kind == TOK_DOT) {
            advance(p);
            if (p->cur.kind != TOK_IDENT) {
                parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "field name",
                                                   token_describe(p->ps->arena, p->cur), "after '.'"));
                return NULL;
            }
            Token name = p->cur;
            advance(p);
            base = expr_field_access(p->ps->arena, span_merge(base->span, token_span(name)),
                                      base, name.lexeme, name.lexeme_len);
        } else if (p->cur.kind == TOK_LBRACKET) {
            advance(p);
            Expr *lo, *hi;
            if (!parse_index_or_slice(p, &lo, &hi)) {
                return NULL;
            }
            if (p->cur.kind != TOK_RBRACKET) {
                parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "']'",
                                                   token_describe(p->ps->arena, p->cur), "after index"));
                return NULL;
            }
            Token rbracket = p->cur;
            advance(p);
            Span full = span_merge(base->span, token_span(rbracket));
            base = hi ? expr_slice(p->ps->arena, full, base, lo, hi)
                      : expr_index(p->ps->arena, full, base, lo);
        } else {
            break;
        }
    }

    return base;
}

static bool is_prim_type_keyword(TokenKind k) {
    return k == TOK_KW_U8 || k == TOK_KW_U16 || k == TOK_KW_U32 || k == TOK_KW_U64 ||
           k == TOK_KW_I8 || k == TOK_KW_I16 || k == TOK_KW_I32 || k == TOK_KW_I64 ||
           k == TOK_KW_BOOL;
}

/* type ::= PRIM_TYPE | IDENT | '[' INTEGER ']' type | '[' ']' type ;
 * A primitive type keyword builds the same TypeIdent node an IDENT used to
 * (the name is the keyword's text), so the dump is unchanged. `unit` is a
 * keyword but not a type: it falls through to the generic "expected type"
 * error like any other non-type token.
 * Self-recursive (array/slice element types nest), same character as
 * unary's self-recursion - its own depth_enter/depth_exit lives directly
 * at this function, not relied on from anywhere else, since this cycle
 * never touches parse_expression at all. */
static Type *parse_type(Parser *p) {
    depth_enter(p->ps, token_span(p->cur));
    Type *result = NULL;

    if (p->cur.kind == TOK_LBRACKET) {
        Token lbracket = p->cur;
        advance(p);
        if (p->cur.kind == TOK_RBRACKET) {
            advance(p);
            Type *elem = parse_type(p);
            if (elem) {
                result = type_slice(p->ps->arena, span_merge(token_span(lbracket), elem->span), elem);
            }
        } else if (p->cur.kind == TOK_NUMBER) {
            Token size_tok = p->cur;
            advance(p);
            if (p->cur.kind != TOK_RBRACKET) {
                parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "']'",
                                                   token_describe(p->ps->arena, p->cur), "after array size"));
            } else {
                advance(p);
                Type *elem = parse_type(p);
                if (elem) {
                    result = type_array(p->ps->arena, span_merge(token_span(lbracket), elem->span),
                                         size_tok.number_value, elem);
                }
            }
        } else {
            parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "array size or ']'",
                                               token_describe(p->ps->arena, p->cur), NULL));
        }
    } else if (p->cur.kind == TOK_IDENT || is_prim_type_keyword(p->cur.kind)) {
        Token t = p->cur;
        advance(p);
        result = type_ident(p->ps->arena, token_span(t), t.lexeme, t.lexeme_len);
    } else {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "type",
                                           token_describe(p->ps->arena, p->cur), NULL));
    }

    depth_exit(p->ps);
    return result;
}

/* let_stmt ::= 'let' IDENT [ ':' type ] '=' expression ';' ; */
static Stmt *parse_let_stmt(Parser *p) {
    Token let_tok = p->cur;
    advance(p); /* consume 'let' */

    if (p->cur.kind != TOK_IDENT) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "identifier",
                                           token_describe(p->ps->arena, p->cur), "after 'let'"));
        return NULL;
    }
    Token name = p->cur;
    advance(p);

    Type *type = NULL;
    if (p->cur.kind == TOK_COLON) {
        advance(p);
        type = parse_type(p);
        if (!type) {
            return NULL;
        }
    }

    if (p->cur.kind != TOK_ASSIGN) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "'='",
                                           token_describe(p->ps->arena, p->cur),
                                           type ? "after type" : "after identifier"));
        return NULL;
    }
    advance(p);

    Expr *init = parse_expression(p);
    if (!init) {
        return NULL;
    }

    if (p->cur.kind != TOK_SEMI) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "';'",
                                           token_describe(p->ps->arena, p->cur), "after expression"));
        return NULL;
    }
    Token semi = p->cur;
    advance(p);

    return stmt_let(p->ps->arena, span_merge(token_span(let_tok), token_span(semi)),
                     name.lexeme, name.lexeme_len, type, init);
}

/* return_stmt ::= 'return' [ expression ] ';' ; */
static Stmt *parse_return_stmt(Parser *p) {
    Token return_tok = p->cur;
    advance(p); /* consume 'return' */

    Expr *value = NULL;
    if (p->cur.kind != TOK_SEMI) {
        value = parse_expression(p);
        if (!value) {
            return NULL;
        }
    }

    if (p->cur.kind != TOK_SEMI) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "';'",
                                           token_describe(p->ps->arena, p->cur), "after expression"));
        return NULL;
    }
    Token semi = p->cur;
    advance(p);

    return stmt_return(p->ps->arena, span_merge(token_span(return_tok), token_span(semi)), value);
}

/* break_stmt ::= 'break' ';' ; */
static Stmt *parse_break_stmt(Parser *p) {
    Token break_tok = p->cur;
    advance(p); /* consume 'break' */

    if (p->cur.kind != TOK_SEMI) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "';'",
                                           token_describe(p->ps->arena, p->cur), "after 'break'"));
        return NULL;
    }
    Token semi = p->cur;
    advance(p);

    return stmt_break(p->ps->arena, span_merge(token_span(break_tok), token_span(semi)));
}

/* continue_stmt ::= 'continue' ';' ; */
static Stmt *parse_continue_stmt(Parser *p) {
    Token continue_tok = p->cur;
    advance(p); /* consume 'continue' */

    if (p->cur.kind != TOK_SEMI) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "';'",
                                           token_describe(p->ps->arena, p->cur), "after 'continue'"));
        return NULL;
    }
    Token semi = p->cur;
    advance(p);

    return stmt_continue(p->ps->arena, span_merge(token_span(continue_tok), token_span(semi)));
}

/* postfix_stmt ::= postfix expr_tail ';' ;  for_step ::= postfix expr_tail ;
 * Shared by both (M2_PLAN.md: "Same left-factoring as postfix_stmt,
 * reusing the same expr_tail") - identical dispatch, differing only in
 * whether a trailing ';' is required (postfix_stmt: yes; for_step: no,
 * the block's '{' follows directly). One token of lookahead right after
 * the shared `postfix` prefix - '=' or not - decides AssignStmt vs.
 * ExprStmt; this is the actual LL(1) decision point, not the statement-
 * level FIRST-set split the grammar used to have (see M2_PLAN.md S4). */
static Stmt *parse_postfix_expr_tail(Parser *p, bool require_semi) {
    Span start = token_span(p->cur);
    Expr *lhs = parse_postfix(p);
    if (!lhs) return NULL;

    bool is_assign = (p->cur.kind == TOK_ASSIGN);
    Expr *rhs = NULL;
    Expr *full_expr = NULL;

    if (is_assign) {
        advance(p);
        rhs = parse_expression(p); /* fresh - assignment's RHS is a full expression, not a continuation */
        if (!rhs) return NULL;
    } else {
        full_expr = parse_expr_tail(p, lhs);
        if (!full_expr) return NULL;
    }

    Span end_span = is_assign ? rhs->span : full_expr->span;

    if (require_semi) {
        if (p->cur.kind != TOK_SEMI) {
            parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "';'",
                                               token_describe(p->ps->arena, p->cur), "after expression"));
            return NULL;
        }
        end_span = token_span(p->cur);
        advance(p);
    }

    Span full_span = span_merge(start, end_span);
    return is_assign ? stmt_assign(p->ps->arena, full_span, lhs, rhs)
                      : stmt_expr(p->ps->arena, full_span, full_expr);
}

static Stmt *parse_postfix_stmt(Parser *p) {
    return parse_postfix_expr_tail(p, true);
}

static Stmt *parse_for_step(Parser *p) {
    return parse_postfix_expr_tail(p, false);
}

/* expr_stmt ::= expression ';' ; - reached only for statements that can't
 * start with a postfix (a bare unary prefix, "!flag;", "-x;"); postfix_
 * stmt and expr_stmt are disjoint by construction, decided by one token
 * of lookahead (starts_postfix) before either is attempted. */
static bool starts_postfix(TokenKind k) {
    return k == TOK_NUMBER || k == TOK_KW_TRUE || k == TOK_KW_FALSE || k == TOK_IDENT || k == TOK_LPAREN ||
           k == TOK_LBRACKET || is_prim_type_keyword(k);
}

static Stmt *parse_expr_stmt(Parser *p) {
    Span start = token_span(p->cur);
    Expr *e = parse_expression(p);
    if (!e) return NULL;
    if (p->cur.kind != TOK_SEMI) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "';'",
                                           token_describe(p->ps->arena, p->cur), "after expression"));
        return NULL;
    }
    Token semi = p->cur;
    advance(p);
    return stmt_expr(p->ps->arena, span_merge(start, token_span(semi)), e);
}

static Stmt *parse_postfix_or_expr_stmt(Parser *p) {
    return starts_postfix(p->cur.kind) ? parse_postfix_stmt(p) : parse_expr_stmt(p);
}

/* ---------------------------------------------------------------- *
 * Step 10b: Wirth-style follow-set error recovery (docs/M2_PLAN.md's
 * "Error recovery" table has the level -> resume-token mapping this
 * implements). Replaces Step 10's ad-hoc per-level recovery functions
 * (recover_to_statement_boundary, recover_to_top_level_boundary, the
 * header-specific recover_header_to_body/recover_header_error) with one
 * systematic mechanism, after Step 10's own design was found to have a
 * real gap: a header-recovery fallback could hand a token to the
 * generic statement-level recovery that then swallowed an entire
 * subsequent valid top-level function, because that recovery couldn't
 * tell "a statement illegally starting with 'fn'" (genuinely zero
 * progress, must discard) apart from "we've drifted here via some
 * outer recovery, and this 'fn' is the next real function" (must leave
 * alone). The follow-set mechanism removes the need to guess: each
 * level declares its OWN resume set; ParserState tracks which levels
 * are CURRENTLY active (pushed on entry, popped on exit - a counter per
 * level, not a stack of distinct sets, since every instance of a level
 * shares the same set regardless of nesting depth); one shared skip
 * searches for the first depth-0 token matching ANY currently-active
 * set; and the level whose OWN call site this was (the one that just
 * detected a failure) checks whether the token landed on is ITS OWN -
 * if so, it handles it (locally, however "resuming" means for that
 * level); if not, it does nothing further and simply propagates
 * failure, exactly as plain NULL-propagation always did. Whichever
 * level truly owns the token recognizes it via its OWN identical check,
 * once control has naturally unwound that far - the skip itself only
 * ever runs ONCE per original failure, since the very first check
 * ("already at a sync point?") is false until the skip runs, then true
 * for every subsequent level's check as control unwinds past it.
 *
 * This is also what fixed, in passing, the bug header recovery was
 * built for (docs/M2_PLAN.md's former "Known limitations" entry, now
 * removed): a malformed for-init's header level owns only '{', so a
 * skip that finds the real body opens it directly, letting the body's
 * own errors surface without ever reinterpreting the orphaned cond/
 * step clauses as fresh statements. */

/* Every one of these keywords is unconditionally consumed as the very
 * first action of its own parse_*_stmt function - so a statement failure
 * can never leave `cur` sitting on one of these with zero tokens
 * consumed overall (unlike 'fn'/'struct'/'[', which have no statement-
 * level dispatch handler at all and so CAN fail with zero consumption).
 * That makes it always safe to treat these as the statement level's own
 * resume points: a statement can't legally start with any of them, so
 * seeing one here IS the mistake, not a boundary to preserve - the
 * caller leaves it alone and retries fresh. Found empirically, not by
 * reasoning alone, back in Step 10: an earlier version didn't treat
 * these as sync points, and a skip in progress could run straight
 * through an entire subsequent valid statement that happened to start
 * with one of these, silently dropping it. */
static bool is_statement_start_keyword(TokenKind k) {
    return k == TOK_KW_LET || k == TOK_KW_IF || k == TOK_KW_WHILE || k == TOK_KW_FOR ||
           k == TOK_KW_RETURN || k == TOK_KW_BREAK || k == TOK_KW_CONTINUE;
}

static bool is_statement_resume_token(TokenKind k) {
    return k == TOK_SEMI || is_statement_start_keyword(k);
}

static bool is_block_resume_token(TokenKind k) {
    return k == TOK_RBRACE;
}

static bool is_header_resume_token(TokenKind k) {
    return k == TOK_LBRACE;
}

/* '[' only counts as a top-level resume point when it's '[' IDENT ']'
 * 'fn' - a bare '[' alone is too weak a signal: since #4 step 3 it also
 * starts an array literal (and has always been the index operator), so
 * requiring the full attribute shape is what keeps some unrelated '[' from
 * being treated as a fresh top-level start. The shape is still safe on
 * valid input - `fn` cannot follow an expression, so `[x] fn` is only ever
 * an attribute - and the one invalid-input misfire (an array-shaped
 * `[y]` directly before a real `fn` is claimed as that function's
 * attribute) is pinned by tests/parser/err_recovery_array_shaped_attribute. Peeks up to three tokens ahead via a throwaway copy of the
 * lexer (Lexer is a plain value struct - cheap and safe to copy; this
 * doesn't disturb the real parser's position at all, the same trick
 * parser_main.c's own mode-dispatch peek already uses). */
static bool is_top_level_resume_token(Parser *p) {
    if (p->cur.kind == TOK_KW_FN || p->cur.kind == TOK_KW_STRUCT) {
        return true;
    }
    if (p->cur.kind != TOK_LBRACKET) {
        return false;
    }
    Lexer peek_lx = *p->lx;
    Token t1 = lexer_next(&peek_lx);
    if (t1.kind != TOK_IDENT) {
        return false;
    }
    Token t2 = lexer_next(&peek_lx);
    if (t2.kind != TOK_RBRACKET) {
        return false;
    }
    Token t3 = lexer_next(&peek_lx);
    return t3.kind == TOK_KW_FN;
}

/* docs/M2_PLAN.md's "Error recovery" table is the declared level -> resume
 * -tokens -> masks mapping this mirrors: every level's own ownership check
 * below is a direct, unconditional read of its declared resume predicate,
 * except statement's ';', which is gated on sync_for_header == 0 - the one
 * declared mask in the whole grammar (see parser_state.h's sync_for_header
 * doc comment for why only for_stmt's header needs it). */
static bool is_in_any_active_sync_set(Parser *p) {
    if (p->ps->sync_statement > 0) {
        if (p->cur.kind == TOK_SEMI) {
            if (p->ps->sync_for_header == 0) return true;
        } else if (is_statement_start_keyword(p->cur.kind)) {
            return true;
        }
    }
    if (p->ps->sync_block > 0 && is_block_resume_token(p->cur.kind)) return true;
    if (p->ps->sync_header > 0 && is_header_resume_token(p->cur.kind)) return true;
    if (p->ps->sync_top_level > 0 && is_top_level_resume_token(p)) return true;
    return false;
}

/* Tokens that can never appear inside a well-formed expression, so seeing
 * one proves any '(' or '[' opened earlier in this skip was never closed.
 * The grammar guarantees it (the same reasoning parse_if_stmt's header
 * recovery already relies on - "the grammar guarantees headers never
 * contain '{'"): no `primary` alternative - a literal, a call, a
 * parenthesized expression, and the r1-B conversion / array / repeat
 * forms - contains '{', '}', 'fn', 'struct', or any statement-start
 * keyword (is_statement_start_keyword: "a statement can't legally start
 * with any of them"). */
static bool resets_bracket_mask(TokenKind k) {
    return k == TOK_LBRACE || k == TOK_RBRACE || k == TOK_KW_FN || k == TOK_KW_STRUCT ||
           is_statement_start_keyword(k);
}

/* The one shared skip. Two independent depths, because they gate
 * different things:
 *
 *  - brace_depth ('{'/'}') hides EVERYTHING inside a skipped brace
 *    region: an inner closer opened during THIS skip must not be mistaken
 *    for an active level's own closer (Step 10). A failing '{' itself
 *    counts as an opener - skipping a stray `{ ... }` has to get past its
 *    matching '}'.
 *  - paren_depth ('(' '[' ')' ']', combined) masks ONLY ';': a stray ';'
 *    inside an unclosed call's argument list or an array-repeat literal
 *    "[v; N]" is never a real statement boundary. It does NOT mask '}',
 *    'fn', 'struct', '{' or a statement keyword - and any of those resets
 *    it to 0 (resets_bracket_mask above) - so an unmatched '(' or '[' can
 *    only ever hide a ';', never the rest of the file. (An earlier version
 *    used one combined depth for all three bracket kinds; a lone '(' or
 *    '[' then swallowed every following declaration - external audit of
 *    da9f01c, R2-R5.) The failing token itself never counts as a '(' or
 *    '[' opener: it is the mistake, not a bracket to skip past.
 *
 * Accepted limitation (docs/M2_PLAN.md "Known limitations"): this only
 * sees brackets the skip itself passes over. If the parser already
 * consumed the opener and the error is inside it, `g(1 +; 3)`'s ';' is
 * the failing token, a sync point already, so the skip is a no-op there.
 * Idempotent by construction: if cur is already at a sync point when
 * called, this is a no-op (zero tokens) - which is exactly what makes it
 * safe for every level along an unwind path to call this same function
 * without re-skipping past where an inner level's call already stopped. */
static void sync_skip(Parser *p) {
    if (is_in_any_active_sync_set(p)) {
        return;
    }
    int brace_depth = (p->cur.kind == TOK_LBRACE) ? 1 : 0;
    int paren_depth = 0;
    advance(p); /* guaranteed progress: cur is not already a sync point */
    for (;;) {
        TokenKind k = p->cur.kind;
        if (k == TOK_EOF) {
            return; /* universal hard stop, owned by nobody specifically */
        }
        if (resets_bracket_mask(k)) {
            paren_depth = 0;
        }
        if (brace_depth == 0 && !(k == TOK_SEMI && paren_depth > 0) &&
            is_in_any_active_sync_set(p)) {
            return;
        }
        if (k == TOK_LBRACE) {
            brace_depth++;
        } else if (k == TOK_RBRACE && brace_depth > 0) {
            brace_depth--;
        } else if (k == TOK_LPAREN || k == TOK_LBRACKET) {
            paren_depth++;
        } else if ((k == TOK_RPAREN || k == TOK_RBRACKET) && paren_depth > 0) {
            paren_depth--;
        }
        advance(p);
    }
}

/* Call after any failure within the named level's own parsing. Runs the
 * shared skip (a no-op if some inner level already ran it for this same
 * failure), then reports whether `cur` now belongs to THIS level
 * specifically: true means the caller should handle it (consume it if
 * that's this level's own convention, then resume locally); false means
 * the caller should do nothing further - not even attempt to consume -
 * and just propagate failure upward exactly as always, trusting that
 * whichever level truly owns this token will recognize it via its own
 * call to one of these, once control naturally unwinds that far. */
static bool sync_recover_statement(Parser *p) {
    sync_skip(p);
    return is_statement_resume_token(p->cur.kind);
}

static bool sync_recover_block(Parser *p) {
    sync_skip(p);
    return is_block_resume_token(p->cur.kind);
}

static bool sync_recover_header(Parser *p) {
    sync_skip(p);
    return is_header_resume_token(p->cur.kind);
}

static bool sync_recover_top_level(Parser *p) {
    sync_skip(p);
    return is_top_level_resume_token(p);
}

/* Shared header-failure handling for if/while/for: the caller's own
 * sync_header must already be active (incremented) when this is called,
 * exactly once, regardless of how many header clauses preceded the
 * failure. Runs the recovery search, then pops EVERY counter the caller
 * declared for this header - sync_header always, plus sync_for_header
 * too when `is_for_header` (for_stmt's own ';'-mask, parser_state.h) -
 * at this exact point: right after the search that needs them still
 * active, but strictly BEFORE parsing any recovered body below. Popping
 * sync_for_header any later (it used to happen in a wrapper AFTER this
 * whole function, including after the recovered body was parsed) would
 * leave the mask active while that body's own statements are recovering,
 * wrongly hiding a bare ';' that's genuinely the body's own statement
 * boundary - found by deliberately testing the two-statements-in-one-
 * recovered-body shape, not by inspection (tests/parser/
 * err_recovery_for_mask_not_leaked_into_body).
 *
 * Then checks whether the search landed on this header's own '{' - if
 * so, parses the body anyway so ITS errors still surface (the grammar
 * guarantees headers never contain '{', so stopping at '{' is always
 * safe - docs/M2_PLAN.md's Step 10b table), even though the enclosing
 * if/while/for is still dropped from the AST either way, and sets
 * recovered_past_body so the enclosing parse_block's loop knows not to
 * attempt its OWN sync_skip on whatever already-legitimate token follows
 * (see parser_state.h's doc comment on that field, and parse_block
 * below - this is the fix for a real swallowed-valid-statement bug,
 * covered for if/while/for and for an else-if chain in tests/parser/
 * err_recovery_*_header_recovered_next_stmt_preserved).
 *
 * If recovery instead landed on some OUTER level's token (this header's
 * '{' was never found), does nothing further - the caller just
 * propagates NULL, trusting the outer level to recognize its own token
 * once control unwinds there. Always returns NULL, so every call site
 * can just `return handle_header_failure(p);` (or
 * `handle_for_header_failure(p)` for for_stmt). */
static Stmt *handle_header_failure_impl(Parser *p, bool is_for_header) {
    bool found_body = sync_recover_header(p);
    p->ps->sync_header--;
    if (is_for_header) {
        p->ps->sync_for_header--;
    }
    if (found_body) {
        (void)parse_block(p);
        p->ps->recovered_past_body = true;
    }
    return NULL;
}

static Stmt *handle_header_failure(Parser *p) {
    return handle_header_failure_impl(p, false);
}

/* After an `if` header failed and its body was recovered past
 * (recovered_past_body is set - only handle_header_failure_impl sets it,
 * and only when it found the body), the construct is not over yet:
 * `if x = 5 { a; } else { b; }` still has its else branch. Leaving it
 * unconsumed made parse_block's loop (which, correctly, does not skip
 * after a recovered body) try to parse `else` as a statement - a second,
 * spurious diagnostic from the same mistake - and for an `else if` chain
 * the skip that followed stopped at the `if` and parsed the rest of the
 * chain as a stray statement of its own (external audit of da9f01c, R1).
 * Consumes the same `'else' ( if_stmt | block )` the success path would,
 * dropping the result like the rest of the dropped statement, so any
 * error inside it still surfaces. Depth-wise this adds no cycle: it
 * re-enters parse_if_stmt/parse_block, both already counted. */
static void recover_else_branch(Parser *p) {
    if (!p->ps->recovered_past_body || p->cur.kind != TOK_KW_ELSE) {
        return;
    }
    advance(p);
    /* The flag means "the CURRENT statement's recovery already parsed a
     * body"; it must not be visible to the else branch's own parse. Left
     * set, the first statement failure inside the branch (`else { b d; }`)
     * made the branch's parse_block consume it, skipping its own sync (so
     * `b );` cascaded into a second diagnostic) and leaving it cleared for
     * the enclosing block, which then re-synced and dropped the next valid
     * statement (external audit of v0, finding 1). Clear it for the
     * branch, then set it again: whatever the branch did, the dropped `if`
     * as a whole did recover past a body. */
    p->ps->recovered_past_body = false;
    (void)((p->cur.kind == TOK_KW_IF) ? parse_if_stmt(p) : parse_block(p));
    p->ps->recovered_past_body = true;
}

/* if_stmt ::= 'if' expression block [ 'else' ( if_stmt | block ) ] ;
 * Carries its own depth_enter/depth_exit unconditionally, wrapping the
 * whole function - needed for the else-if self-recursion (if -> else ->
 * if), a cycle that does NOT pass through parse_block at all (an "else
 * if" has no block of its own; only a bare "else { }" does). Every
 * invocation corresponds to a real 'if' in the source, unlike unary's
 * cheap non-recursive fallthrough case, so there's no calibration reason
 * to make the hook conditional the way unary's is. */
static Stmt *parse_if_stmt(Parser *p) {
    depth_enter(p->ps, token_span(p->cur));

    Token if_tok = p->cur;
    advance(p); /* consume 'if' */

    p->ps->sync_header++;
    Expr *cond = parse_expression(p);
    if (!cond) {
        Stmt *dropped = handle_header_failure(p);
        recover_else_branch(p);
        depth_exit(p->ps);
        return dropped;
    }

    /* Dedicated diagnostic (M2_PLAN.md "Dedicated diagnostics"): condition
     * is `expression`, which structurally can never produce '=' - so
     * finding '=' right where the block's '{' is expected is always
     * exactly this mistake, not a generic syntax error. */
    if (p->cur.kind == TOK_ASSIGN) {
        parser_error(p->ps, diag_assignment_as_condition(p->ps->arena, token_span(p->cur), "'{'"));
        Stmt *dropped = handle_header_failure(p);
        recover_else_branch(p);
        depth_exit(p->ps);
        return dropped;
    }
    p->ps->sync_header--;

    Stmt *then_branch = parse_block(p);
    if (!then_branch) {
        depth_exit(p->ps);
        return NULL;
    }

    Stmt *else_branch = NULL;
    if (p->cur.kind == TOK_KW_ELSE) {
        advance(p);
        else_branch = (p->cur.kind == TOK_KW_IF) ? parse_if_stmt(p) : parse_block(p);
        if (!else_branch) {
            depth_exit(p->ps);
            return NULL;
        }
    }

    Span full_span = span_merge(token_span(if_tok), (else_branch ? else_branch->span : then_branch->span));
    Stmt *result = stmt_if(p->ps->arena, full_span, cond, then_branch, else_branch);
    depth_exit(p->ps);
    return result;
}

/* while_stmt ::= 'while' expression block ; - no self-recursion, no new
 * cycle of its own (while -> block -> statement -> while already passes
 * through parse_block's hook), so no depth hook needed here. */
static Stmt *parse_while_stmt(Parser *p) {
    Token while_tok = p->cur;
    advance(p); /* consume 'while' */

    p->ps->sync_header++;
    Expr *cond = parse_expression(p);
    if (!cond) {
        return handle_header_failure(p);
    }

    if (p->cur.kind == TOK_ASSIGN) {
        parser_error(p->ps, diag_assignment_as_condition(p->ps->arena, token_span(p->cur), "'{'"));
        return handle_header_failure(p);
    }
    p->ps->sync_header--;

    Stmt *body = parse_block(p);
    if (!body) return NULL;

    return stmt_while(p->ps->arena, span_merge(token_span(while_tok), body->span), cond, body);
}

/* for_stmt's init clause: let_stmt | postfix_stmt | expr_stmt only - not
 * the full `statement` alternative set (if/while/for/return/break/
 * continue make no sense as a loop initializer). Each of the three
 * already consumes its own trailing ';', which is also for_stmt's own
 * init/cond separator - no extra ';' handling needed here. */
static Stmt *parse_for_init(Parser *p) {
    if (p->cur.kind == TOK_KW_LET) {
        return parse_let_stmt(p);
    }
    return parse_postfix_or_expr_stmt(p);
}

/* for_stmt ::= 'for' ( let_stmt | postfix_stmt | expr_stmt ) expression
 *              ';' for_step block ;
 * No parens around the clauses (confirmed against the grammar as
 * written - "for let i = 0; i < n; i = i + 1 { ... }", not "for (...)").
 * No self-recursion, no new cycle of its own beyond for -> block ->
 * statement -> for (already counted via parse_block), so no depth hook
 * needed here, same reasoning as while_stmt. */
/* for_stmt's own header-failure entry point - see handle_header_failure_
 * impl's doc comment just above for what `is_for_header` changes. */
static Stmt *handle_for_header_failure(Parser *p) {
    return handle_header_failure_impl(p, true);
}

static Stmt *parse_for_stmt(Parser *p) {
    Token for_tok = p->cur;
    advance(p); /* consume 'for' */

    /* sync_header covers the WHOLE header (init, cond, the ';' separator,
     * and step) as a single active span, not one push/pop per clause -
     * this is what fixed the former "known limitation" for-init double
     * diagnostic: a malformed init no longer lets cond/step get
     * reinterpreted as fresh statements, because any failure anywhere in
     * the header immediately looks for THIS header's own '{' first.
     * sync_for_header rides along, declaring the ';'-mask for exactly the
     * same span (see parser_state.h). */
    p->ps->sync_header++;
    p->ps->sync_for_header++;

    Stmt *init = parse_for_init(p);
    if (!init) {
        return handle_for_header_failure(p);
    }

    Expr *cond = parse_expression(p);
    if (!cond) {
        return handle_for_header_failure(p);
    }

    /* Same dedicated diagnostic as if/while's condition (M2_PLAN.md
     * "Dedicated diagnostics") - for's cond position is the bare
     * `expression` grammar rule same as theirs, so "i = 5" here is
     * exactly the same assignment-as-condition mistake, not a different
     * one. Previously fell through to the generic "expected ';' after
     * condition, found '='" below instead, which is technically true but
     * doesn't name the actual mistake the way if/while's check already
     * does. */
    if (p->cur.kind == TOK_ASSIGN) {
        parser_error(p->ps, diag_assignment_as_condition(p->ps->arena, token_span(p->cur), "';'"));
        return handle_for_header_failure(p);
    }

    if (p->cur.kind != TOK_SEMI) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "';'",
                                           token_describe(p->ps->arena, p->cur), "after condition"));
        return handle_for_header_failure(p);
    }
    advance(p);

    Stmt *step = parse_for_step(p);
    if (!step) {
        return handle_for_header_failure(p);
    }
    p->ps->sync_header--;
    p->ps->sync_for_header--;

    Stmt *body = parse_block(p);
    if (!body) return NULL;

    return stmt_for(p->ps->arena, span_merge(token_span(for_tok), body->span), init, cond, step, body);
}

/* statement ::= let_stmt | if_stmt | while_stmt | for_stmt | return_stmt
 *             | break_stmt | continue_stmt | postfix_stmt | expr_stmt ;
 * Full set as of Step 7. Anything else in statement position falls
 * through to parse_postfix_or_expr_stmt, which itself produces a clean
 * generic diagnostic (via parse_expression -> ... -> parse_primary) for
 * genuinely unparseable input - not a crash or a silently-accepted no-op.
 * This dispatcher itself needs no depth hook (a one-shot switch, not a
 * cycle of its own - same character as Step 5's precedence-pipeline
 * functions). */
static Stmt *parse_statement(Parser *p) {
    switch (p->cur.kind) {
        case TOK_KW_LET:      return parse_let_stmt(p);
        case TOK_KW_IF:       return parse_if_stmt(p);
        case TOK_KW_WHILE:    return parse_while_stmt(p);
        case TOK_KW_FOR:      return parse_for_stmt(p);
        case TOK_KW_RETURN:   return parse_return_stmt(p);
        case TOK_KW_BREAK:    return parse_break_stmt(p);
        case TOK_KW_CONTINUE: return parse_continue_stmt(p);
        default:               return parse_postfix_or_expr_stmt(p);
    }
}

/* True when the current token is `fn` or `struct` AND a top-level level is
 * active to take it. Neither can appear inside a block (v0 has no nested
 * functions or structs), so seeing one means the block is unclosed - a
 * missing '}' is by far the likeliest cause, and the declaration belongs to
 * the top level either way. parse_block treats it as the implicit end of
 * the block: one diagnostic, the statements parsed so far are kept, and the
 * token is left for the top level (previously the block returned NULL, which
 * dropped the whole enclosing function - post-audit finding #0). Gated on
 * sync_top_level like the rest of recovery: a block parsed on its own
 * (parser_parse_block, no top level above it) has nobody to hand the token
 * to, so there it stays an ordinary bad statement. */
static bool at_top_level_start(const Parser *p) {
    return p->ps->sync_top_level > 0 &&
           (p->cur.kind == TOK_KW_FN || p->cur.kind == TOK_KW_STRUCT);
}

/* block ::= '{' { statement } '}' ;
 * Carries its own depth_enter/depth_exit - added proactively in Step 6,
 * before any cycle closed through it (none of let/return/break/continue
 * contain a nested block). As of Step 7, if_stmt/while_stmt/for_stmt each
 * wrap a block, closing block -> statement -> {if,while,for}_stmt -> block
 * cycles that don't pass through parse_expression at all - this hook is
 * what bounds all three. (if_stmt's OWN else-if self-recursion is a
 * separate cycle that doesn't pass through block either - see
 * parse_if_stmt's own hook.) Empirically proven at the cap in
 * tests/parser/depth_nested_if_* and depth_elseif_*. */
static Stmt *parse_block(Parser *p) {
    depth_enter(p->ps, token_span(p->cur));

    /* Unreachable via tests/parser/'s current harness (parser_main.c only
     * calls parser_parse_block after peeking a '{' as the first token), so
     * it doesn't show as covered by make coverage - not untested by
     * accident, just not yet reachable from the one entry point that
     * exists. Step 7's if_stmt/while_stmt/for_stmt call parse_block
     * directly after parsing a condition expression, where this check
     * becomes genuinely reachable (a missing '{' after the condition) and
     * will get real coverage then. Keeping the check now rather than
     * adding it only once something can reach it, same reasoning as
     * building every parse function's error handling in from the start. */
    if (p->cur.kind != TOK_LBRACE) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "'{'",
                                           token_describe(p->ps->arena, p->cur), NULL));
        depth_exit(p->ps);
        return NULL;
    }
    Token lbrace = p->cur;
    advance(p);

    /* block and statement share one lifetime (both active for exactly as
     * long as this function is on the stack), but are two DISTINCT sync
     * sets - block owns only '}', statement owns ';'/the statement-start
     * keywords - because a skip triggered by a failure two levels down
     * (e.g. inside an if's header) needs to find whichever of the two
     * comes first, not conflate them into one set. */
    p->ps->sync_block++;
    p->ps->sync_statement++;

    NodeList stmts;
    node_list_init(&stmts);
    bool unclosed_already_reported = false;
    while (p->cur.kind != TOK_RBRACE && p->cur.kind != TOK_EOF && !at_top_level_start(p)) {
        Stmt *s = parse_statement(p);
        if (!s) {
            if (p->ps->recovered_past_body) {
                /* A nested header (this statement itself, or one nested
                 * arbitrarily deep inside its else-if chain) already ran
                 * its own recovery search AND parsed a real recovered
                 * body - cur is wherever that legitimately left it, not
                 * sitting at the point of failure. Attempting ANOTHER
                 * sync_skip from here would treat already-valid content
                 * as "junk to search past" purely because this statement
                 * itself is still being dropped - re-check the loop guard
                 * fresh instead (see parser_state.h's doc comment on this
                 * field). */
                p->ps->recovered_past_body = false;
                continue;
            }
            if (sync_recover_statement(p)) {
                /* ';' is consumed as the boundary itself; a statement-
                 * start keyword is left alone for the next loop iteration
                 * to retry fresh (it was never the mistake). */
                if (p->cur.kind == TOK_SEMI) {
                    advance(p);
                }
                continue;
            }
            if (sync_recover_block(p)) {
                /* Not a failure - skipping landed on this block's own
                 * closing '}' (e.g. via an inner header's unresolved
                 * failure). The while condition below will see it. */
                break;
            }
            if (at_top_level_start(p)) {
                /* A skip landed on `fn`/`struct`: end the block here and
                 * keep the statements parsed so far. This statement's own
                 * error is already reported, and it is what led here, so
                 * the unclosed block is not reported a second time (one
                 * mistake, one diagnostic - the Step 10b rule). */
                unclosed_already_reported = true;
                break;
            }
            /* Owned by neither this statement nor this block - propagate
             * without consuming, trusting an outer level (another block,
             * a header, or the top level) to recognize it once control
             * unwinds that far. */
            p->ps->sync_statement--;
            p->ps->sync_block--;
            depth_exit(p->ps);
            return NULL;
        }
        node_list_push(p->ps->arena, &stmts, s);
    }
    p->ps->sync_statement--;
    p->ps->sync_block--;

    if (p->cur.kind != TOK_RBRACE) {
        bool repeat = at_top_level_start(p) && p->ps->unclosed_line == p->cur.line &&
                      p->ps->unclosed_col == p->cur.col;
        if (!unclosed_already_reported && !repeat) {
            if (at_top_level_start(p)) {
                p->ps->unclosed_line = p->cur.line;
                p->ps->unclosed_col = p->cur.col;
            }
            parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "'}'",
                                               token_describe(p->ps->arena, p->cur), "to close block"));
        }
        if (at_top_level_start(p)) {
            /* Keep what was parsed: the block ends at its last statement
             * (or at the '{' if it has none). The `fn`/`struct` is left
             * for the top level. */
            Span end = stmts.len > 0 ? ((const Stmt *)stmts.items[stmts.len - 1])->span : token_span(lbrace);
            Stmt *partial = stmt_block(p->ps->arena, span_merge(token_span(lbrace), end), stmts);
            depth_exit(p->ps);
            return partial;
        }
        depth_exit(p->ps);
        return NULL;
    }
    Token rbrace = p->cur;
    advance(p);

    Stmt *result = stmt_block(p->ps->arena, span_merge(token_span(lbrace), token_span(rbrace)), stmts);
    depth_exit(p->ps);
    return result;
}

/* version_decl ::= 'manna' IDENT ';' ;
 * Required as the first non-comment line of every file (MANNA_PRINCIPLES.md
 * Part 4 item 1) - a missing or wrong version is a compile error, not a
 * warning. Four distinct mistakes, four distinct diagnostics: the file's
 * very first token being a LEXER error (bad byte, unterminated comment,
 * malformed/overflowing number - is_lexer_error_token) is the generic
 * diag_expected (E0105) naming that actual problem, not E0107 - a lexer
 * error isn't "manna is missing", it's "the file couldn't even be
 * tokenized here", a different mistake that deserves its own wording
 * rather than a misleading "add manna v0;" fix-it over text that isn't
 * valid source to begin with. 'manna' keyword altogether absent (and not
 * a lexer error) is the dedicated diag_missing_version_header (E0107,
 * with a fix-it inserting "manna v0;\n" at 1:1); 'manna' present but
 * malformed after that (no identifier, no ';') is the generic
 * diag_expected (E0105); 'manna' present and well-formed but the wrong
 * version is the dedicated diag_unsupported_version (E0106). No AST
 * node: version_decl is a compile-time gate, not part of the semantic
 * tree (docs/M2_PLAN.md's AST node types section has no such node). No
 * depth hook: not recursive, called exactly once per file. */
static bool parse_version_decl(Parser *p) {
    if (is_lexer_error_token(p->cur.kind)) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "'manna' version declaration",
                                           token_describe(p->ps->arena, p->cur), NULL));
        return false;
    }
    if (p->cur.kind != TOK_KW_MANNA) {
        parser_error(p->ps, diag_missing_version_header(p->ps->arena, token_span(p->cur)));
        return false;
    }
    advance(p);

    if (p->cur.kind != TOK_IDENT) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "version identifier",
                                           token_describe(p->ps->arena, p->cur), "after 'manna'"));
        return false;
    }
    Token version_tok = p->cur;
    bool version_ok = version_tok.lexeme_len == 2 && version_tok.lexeme[0] == 'v' && version_tok.lexeme[1] == '0';
    advance(p);

    if (!version_ok) {
        parser_error(p->ps, diag_unsupported_version(p->ps->arena, token_span(version_tok),
                                                       version_tok.lexeme, version_tok.lexeme_len));
        return false;
    }

    if (p->cur.kind != TOK_SEMI) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "';'",
                                           token_describe(p->ps->arena, p->cur), "after version"));
        return false;
    }
    advance(p);
    return true;
}

/* field_decl ::= IDENT ':' type ',' ;
 * The trailing comma is part of this production, not a separator between
 * productions - every field needs one, including the last (no special-
 * casing "last field, no comma"), per M2_PLAN.md's grammar as written. */
static Field *parse_field_decl(Parser *p) {
    if (p->cur.kind != TOK_IDENT) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "field name",
                                           token_describe(p->ps->arena, p->cur), NULL));
        return NULL;
    }
    Token name = p->cur;
    advance(p);

    if (p->cur.kind != TOK_COLON) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "':'",
                                           token_describe(p->ps->arena, p->cur), "after field name"));
        return NULL;
    }
    advance(p);

    Type *type = parse_type(p);
    if (!type) return NULL;

    if (p->cur.kind != TOK_COMMA) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "','",
                                           token_describe(p->ps->arena, p->cur), "after field type"));
        return NULL;
    }
    Token comma = p->cur;
    advance(p);

    return field_new(p->ps->arena, span_merge(token_span(name), token_span(comma)), name.lexeme, name.lexeme_len, type);
}

/* struct_decl ::= 'struct' IDENT '{' { field_decl } '}' ;
 * No depth hook: fields don't recurse into struct_decl or top_level_item,
 * so this isn't on any cycle (same reasoning as parse_statement - a flat
 * dispatch/loop, not a cycle of its own). */
static Decl *parse_struct_decl(Parser *p) {
    Token struct_tok = p->cur;
    advance(p); /* consume 'struct' */

    if (p->cur.kind != TOK_IDENT) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "identifier",
                                           token_describe(p->ps->arena, p->cur), "after 'struct'"));
        return NULL;
    }
    Token name = p->cur;
    advance(p);

    if (p->cur.kind != TOK_LBRACE) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "'{'",
                                           token_describe(p->ps->arena, p->cur), "after struct name"));
        return NULL;
    }
    advance(p);

    NodeList fields;
    node_list_init(&fields);
    while (p->cur.kind != TOK_RBRACE && p->cur.kind != TOK_EOF) {
        Field *f = parse_field_decl(p);
        if (!f) return NULL;
        node_list_push(p->ps->arena, &fields, f);
    }

    if (p->cur.kind != TOK_RBRACE) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "'}'",
                                           token_describe(p->ps->arena, p->cur), "to close struct"));
        return NULL;
    }
    Token rbrace = p->cur;
    advance(p);

    return decl_struct(p->ps->arena, span_merge(token_span(struct_tok), token_span(rbrace)),
                        name.lexeme, name.lexeme_len, fields);
}

/* attribute ::= '[' IDENT ']' ; - the parser accepts any identifier here;
 * whether it names a recognized attribute (e.g. "kernel_hook") is an
 * IR-stage check (docs/M2_PLAN.md S7 item 8), not implemented here. */
static bool parse_attribute(Parser *p, const char **out_name) {
    advance(p); /* consume '[' */

    if (p->cur.kind != TOK_IDENT) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "attribute name",
                                           token_describe(p->ps->arena, p->cur), "after '['"));
        return false;
    }
    Token name = p->cur;
    advance(p);

    if (p->cur.kind != TOK_RBRACKET) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "']'",
                                           token_describe(p->ps->arena, p->cur), "after attribute name"));
        return false;
    }
    advance(p);

    *out_name = arena_sprintf(p->ps->arena, "%.*s", (int)name.lexeme_len, name.lexeme);
    return true;
}

/* param ::= IDENT ':' type ; */
static Param *parse_param(Parser *p) {
    if (p->cur.kind != TOK_IDENT) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "parameter name",
                                           token_describe(p->ps->arena, p->cur), NULL));
        return NULL;
    }
    Token name = p->cur;
    advance(p);

    if (p->cur.kind != TOK_COLON) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "':'",
                                           token_describe(p->ps->arena, p->cur), "after parameter name"));
        return NULL;
    }
    advance(p);

    Type *type = parse_type(p);
    if (!type) return NULL;

    return field_new(p->ps->arena, span_merge(token_span(name), type->span), name.lexeme, name.lexeme_len, type);
}

/* param_list ::= param { ',' param } ; - comma-SEPARATED, not self-
 * terminated like field_decl: no trailing comma after the last param. */
static bool parse_param_list(Parser *p, NodeList *out_params) {
    node_list_init(out_params);
    for (;;) {
        Param *param = parse_param(p);
        if (!param) return false;
        node_list_push(p->ps->arena, out_params, param);
        if (p->cur.kind == TOK_COMMA) {
            advance(p);
            continue;
        }
        break;
    }
    return true;
}

/* function_decl ::= { attribute } 'fn' IDENT '(' [ param_list ] ')'
 *                   [ '->' type ] block ;
 * Absent '-> type' means unit, stated explicitly in the AST (return_type
 * = NULL), never inferred - function signatures are never one of the
 * "inside a function body" contexts principle 15 permits inference in.
 * No depth hook of its own: the only recursion here is the body, which
 * is a block (already counted). */
/* One-token lookahead past the current token, on a throwaway copy of the
 * lexer (a plain value struct - see lexer.h): is the NEXT token '{'? */
static bool token_after_is_lbrace(Parser *p) {
    Lexer peek_lx = *p->lx;
    return lexer_next(&peek_lx).kind == TOK_LBRACE;
}

static Decl *parse_function_decl(Parser *p) {
    Span start = token_span(p->cur); /* first attribute's '[', or 'fn' if none */

    NodeList attributes;
    node_list_init(&attributes);
    while (p->cur.kind == TOK_LBRACKET) {
        const char *attr_name;
        if (!parse_attribute(p, &attr_name)) return NULL;
        node_list_push(p->ps->arena, &attributes, (void *)attr_name);
    }

    if (p->cur.kind != TOK_KW_FN) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "'fn'",
                                           token_describe(p->ps->arena, p->cur), NULL));
        return NULL;
    }
    advance(p);

    if (p->cur.kind != TOK_IDENT) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "identifier",
                                           token_describe(p->ps->arena, p->cur), "after 'fn'"));
        return NULL;
    }
    Token name = p->cur;
    advance(p);

    if (p->cur.kind != TOK_LPAREN) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "'('",
                                           token_describe(p->ps->arena, p->cur), "after function name"));
        return NULL;
    }
    advance(p);

    NodeList params;
    if (p->cur.kind == TOK_RPAREN) {
        node_list_init(&params);
    } else if (!parse_param_list(p, &params)) {
        return NULL;
    }

    if (p->cur.kind != TOK_RPAREN) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "')'",
                                           token_describe(p->ps->arena, p->cur), "after parameters"));
        return NULL;
    }
    advance(p);

    Type *return_type = NULL;
    if (p->cur.kind == TOK_ARROW) {
        Token arrow = p->cur;
        advance(p);
        if (p->cur.kind == TOK_KW_UNIT && token_after_is_lbrace(p)) {
            /* `-> unit {`: E0109 (dedicated; "omit the return type"). The
             * function STAYS in the tree: the annotation is dropped (absent
             * return type) and the body is parsed normally, so errors in it
             * still surface. Only this exact shape - `-> [3]unit` and
             * `-> unit[...]` are not "writing unit as the return type" and
             * stay the generic "expected type" (E0105) from parse_type. */
            Token unit = p->cur;
            const char *gap_start = arrow.lexeme + arrow.lexeme_len;
            bool blanks = arrow.line == unit.line;
            for (const char *c = gap_start; blanks && c < unit.lexeme; c++) {
                if (*c != ' ' && *c != '\t') blanks = false;
            }
            parser_error(p->ps, diag_return_unit(p->ps->arena, token_span(arrow), token_span(unit), blanks));
            advance(p); /* consume 'unit'; return_type stays NULL */
        } else {
            return_type = parse_type(p);
            if (!return_type) return NULL;
        }
    }

    Stmt *body = parse_block(p);
    if (!body) return NULL;

    return decl_function(p->ps->arena, span_merge(start, body->span), attributes,
                          name.lexeme, name.lexeme_len, params, return_type, body);
}

/* top_level_item ::= struct_decl | function_decl ;
 * Flat dispatch, not a cycle of its own (same character as parse_
 * statement) - the loop over top_level_item lives in parser_parse_file,
 * not here, since there's no recursive "top_level_item inside a
 * top_level_item" to speak of. */
static Decl *parse_top_level_item(Parser *p) {
    if (p->cur.kind == TOK_KW_STRUCT) {
        return parse_struct_decl(p);
    }
    if (p->cur.kind == TOK_KW_FN || p->cur.kind == TOK_LBRACKET) {
        return parse_function_decl(p);
    }
    parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "struct or function declaration",
                                       token_describe(p->ps->arena, p->cur), NULL));
    return NULL;
}

/* Every public entry point resets ALL per-parse state (depth, error_count,
 * the sync_* counters, recovered_past_body) before parsing a token:
 * parser_state_init zeroes it once, but a ParserState that gets reused for
 * a second parse call, or recovered into after a longjmp to fatal_target
 * (which skips every depth_exit and every sync_*-- decrement a normal
 * return would have run), could otherwise start this call with stale
 * state left over from whatever was active when a PREVIOUS call stopped -
 * a stale depth made a perfectly valid second file hit E0103 immediately.
 * These three functions are the only place a parse ever begins, so
 * resetting here covers every case - see parser_state_reset_for_parse's
 * own doc comment. */
Expr *parser_parse_expression(ParserState *ps, Lexer *lx) {
    parser_state_reset_for_parse(ps);
    Parser p;
    p.ps = ps;
    p.lx = lx;
    advance(&p); /* prime the lookahead token */
    return parse_expression(&p);
}

Stmt *parser_parse_block(ParserState *ps, Lexer *lx) {
    parser_state_reset_for_parse(ps);
    Parser p;
    p.ps = ps;
    p.lx = lx;
    advance(&p); /* prime the lookahead token */
    return parse_block(&p);
}

bool parser_parse_file(ParserState *ps, Lexer *lx, NodeList *out_items) {
    parser_state_reset_for_parse(ps);
    Parser p;
    p.ps = ps;
    p.lx = lx;
    advance(&p); /* prime the lookahead token */

    if (!parse_version_decl(&p)) {
        return false;
    }

    node_list_init(out_items);
    p.ps->sync_top_level++;
    while (p.cur.kind != TOK_EOF) {
        Token before = p.cur;
        Decl *item = parse_top_level_item(&p);
        if (!item) {
            /* Result unchecked: there is no OUTER level above top-level to
             * hand an unrecognized token off to - either sync_skip found
             * this level's own fn/struct/'[' (safe to retry fresh) or it
             * ran to EOF, caught by the loop guard on the next
             * iteration. */
            sync_recover_top_level(&p);
            if (p.cur.line == before.line && p.cur.col == before.col) {
                /* Last-resort progress guarantee: every OTHER level's own
                 * resume token is unconditionally consumed by its true
                 * owner (statement/block/header), but a fresh top-level
                 * failure can land exactly on its own dispatch token
                 * (fn/struct/'[') with zero tokens consumed - there's no
                 * inner level left to consume it for us. Skip exactly one
                 * token so the loop can never spin in place forever.
                 *
                 * Unreachable under the current grammar (not untested by
                 * accident): parse_function_decl/parse_struct_decl/parse_
                 * attribute each unconditionally consume their own leading
                 * token ('fn'/'struct'/'[') before they can fail, and the
                 * one case that fails with truly zero consumption (the
                 * "struct or function declaration" default case, for a
                 * token that's none of the three) is, by that same
                 * construction, never already a top-level resume point -
                 * so sync_skip's own "guaranteed progress" advance always
                 * fires first in that case instead. Kept as a real,
                 * enforced backstop rather than relying on that chain of
                 * reasoning staying true forever - same justification as
                 * parse_block's own proactively-added '{' check back in
                 * Step 6, before anything could reach it either. */
#ifdef MANNA_FUZZ_TRACE_LAST_RESORT
                /* Fuzz-only (tools/fuzz/run_fuzzers.sh): if the above
                 * reasoning is ever actually wrong, this makes libFuzzer
                 * report and save the exact input that proved it, instead
                 * of the line only ever being counted after the fact.
                 * Compiled out of every normal build (plain, ASan, the
                 * real binary) - zero behavior change there. */
                __builtin_trap();
#endif
                advance(&p);
            }
            continue; /* dropped - no placeholder node; loop condition
                       * re-checks EOF before trying again */
        }
        node_list_push(ps->arena, out_items, item);
    }
    p.ps->sync_top_level--;
    return true;
}
