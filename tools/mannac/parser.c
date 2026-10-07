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

static Span token_span(Token t) {
    Span s;
    s.start_line = t.line;
    s.start_col = t.col;
    s.end_line = t.line;
    s.end_col = t.col + (int)t.lexeme_len;
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
    switch (t.kind) {
        case TOK_ERROR_BAD_CHAR:
        case TOK_ERROR_UNTERMINATED_COMMENT:
        case TOK_ERROR_MALFORMED_NUMBER:
        case TOK_ERROR_NUMBER_OVERFLOW:
            return t.error_msg;
        default:
            return arena_sprintf(a, "'%.*s'", (int)t.lexeme_len, t.lexeme);
    }
}

/* ---------------------------------------------------------------- *
 * Grammar (docs/M2_PLAN.md S4):
 *
 *   expression     ::= logic_or ;
 *   logic_or       ::= logic_and { '||' logic_and } ;
 *   logic_and      ::= equality { '&&' equality } ;
 *   equality       ::= comparison [ ('=='|'!=') comparison ] ;
 *   comparison     ::= bitor [ ('<'|'>'|'<='|'>=') bitor ] ;
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
 *                     | '(' expression ')'
 *                     ;
 *   arg_list       ::= expression { ',' expression } ;
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
 *   type           ::= IDENT | '[' INTEGER ']' type | '[' ']' type ;
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
 * carry depth_enter/depth_exit - not the 10 fixed-pipeline binary-
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
 * block again) plus exactly seven back-edges, each one a cycle:
 *
 *   1. unary -> unary (self-recursive prefix case, "!!!!x")
 *      Counted by: parse_unary's own hook.
 *   2. primary -> expression (the '(' expression ')' case, "(((x)))")
 *      Counted by: parse_expression's hook.
 *   3. primary -> arg_list -> expression (call args, "f(f(f(x)))")
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
 *
 * Cycles 1-5 were proven at the cap in Steps 5-6 (tests/parser/depth_*);
 * cycle 6 (true if/while/for nesting) and cycle 7 (else-if chains) are
 * proven here (tests/parser/depth_nested_if_*, depth_elseif_*) - both
 * cap below the single-construct 255/256 figures from Steps 5-6, since
 * each level here costs more than 1 depth unit (block's hook, and for
 * cycle 6 also if_stmt's), the same "shared budget" effect Step 6 found
 * for nested array types inside a block.
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
 * NULL and propagates it immediately rather than attempting to continue.
 * There's no resynchronization yet - that's Step 10's job. The binary-
 * operator levels below need no diagnostic calls of their own: a failed
 * operand eventually bottoms out at parse_primary, which already emits
 * "expected expression, found X" - every level above it just propagates
 * NULL.
 * ---------------------------------------------------------------- */

static Expr *parse_expression(Parser *p);
static Expr *parse_logic_or(Parser *p, Expr *given_lhs);
static Expr *parse_logic_and(Parser *p, Expr *given_lhs);
static Expr *parse_equality(Parser *p, Expr *given_lhs);
static Expr *parse_comparison(Parser *p, Expr *given_lhs);
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
    lhs = parse_comparison(p, lhs);     if (!lhs) return NULL;
    lhs = parse_equality(p, lhs);       if (!lhs) return NULL;
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
    Expr *lhs = given_lhs ? given_lhs : parse_equality(p, NULL);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_AND) {
        advance(p);
        Expr *rhs = parse_equality(p, NULL);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), BINOP_AND, lhs, rhs);
    }
    return lhs;
}

/* Non-associative (docs/M2_PLAN.md): comparison/equality each consume AT
 * MOST ONE of their own operator, then check whether the token right
 * after is itself comparison/equality-level - if so, that's unparseable
 * chaining ("a < b < c", "a == b == c", "a < b == c", ...), and gets the
 * dedicated diagnostic right here rather than a confusing generic error
 * from whatever construct happens to be one level up. comparison()'s own
 * check only looks at its OWN 4 operators: a trailing '=='/'!=' after a
 * successfully-parsed comparison is NOT a violation there - it's
 * equality() legitimately about to wrap that comparison as its own LHS
 * (e.g. "a < b == c" is the ordinary, unambiguous "(a<b) == c"). equality()
 * itself checks the full 6-operator set, since nothing above equality
 * understands any of them - a trailing one there has nowhere left to go
 * but a dedicated error. */
static bool is_comparison_op(TokenKind k) {
    return k == TOK_LT || k == TOK_GT || k == TOK_LE || k == TOK_GE;
}

static bool is_comparison_or_equality_op(TokenKind k) {
    return is_comparison_op(k) || k == TOK_EQ || k == TOK_NE;
}

static Expr *parse_equality(Parser *p, Expr *given_lhs) {
    Expr *lhs = given_lhs ? given_lhs : parse_comparison(p, NULL);
    if (!lhs) return NULL;
    if (p->cur.kind == TOK_EQ || p->cur.kind == TOK_NE) {
        BinOp op = p->cur.kind == TOK_EQ ? BINOP_EQ : BINOP_NE;
        advance(p);
        Expr *rhs = parse_comparison(p, NULL);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), op, lhs, rhs);
        if (is_comparison_or_equality_op(p->cur.kind)) {
            parser_error(p->ps, diag_chained_comparison(token_span(p->cur)));
            return NULL;
        }
    }
    return lhs;
}

static Expr *parse_comparison(Parser *p, Expr *given_lhs) {
    Expr *lhs = given_lhs ? given_lhs : parse_bitor(p, NULL);
    if (!lhs) return NULL;
    if (is_comparison_op(p->cur.kind)) {
        BinOp op;
        switch (p->cur.kind) {
            case TOK_LT: op = BINOP_LT; break;
            case TOK_GT: op = BINOP_GT; break;
            case TOK_LE: op = BINOP_LE; break;
            default:     op = BINOP_GE; break; /* TOK_GE */
        }
        advance(p);
        Expr *rhs = parse_bitor(p, NULL);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), op, lhs, rhs);
        if (is_comparison_op(p->cur.kind)) {
            parser_error(p->ps, diag_chained_comparison(token_span(p->cur)));
            return NULL;
        }
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
                    advance(p);
                } else {
                    parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "')'",
                                                       token_describe(p->ps->arena, p->cur), "after expression"));
                    result = NULL;
                }
            }
            break;

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

/* type ::= IDENT | '[' INTEGER ']' type | '[' ']' type ;
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
    } else if (p->cur.kind == TOK_IDENT) {
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
    return k == TOK_NUMBER || k == TOK_KW_TRUE || k == TOK_KW_FALSE || k == TOK_IDENT || k == TOK_LPAREN;
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

    Expr *cond = parse_expression(p);
    if (!cond) {
        depth_exit(p->ps);
        return NULL;
    }

    /* Dedicated diagnostic (M2_PLAN.md "Dedicated diagnostics"): condition
     * is `expression`, which structurally can never produce '=' - so
     * finding '=' right where the block's '{' is expected is always
     * exactly this mistake, not a generic syntax error. */
    if (p->cur.kind == TOK_ASSIGN) {
        parser_error(p->ps, diag_assignment_as_condition(p->ps->arena, token_span(p->cur)));
        depth_exit(p->ps);
        return NULL;
    }

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

    Expr *cond = parse_expression(p);
    if (!cond) return NULL;

    if (p->cur.kind == TOK_ASSIGN) {
        parser_error(p->ps, diag_assignment_as_condition(p->ps->arena, token_span(p->cur)));
        return NULL;
    }

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
static Stmt *parse_for_stmt(Parser *p) {
    Token for_tok = p->cur;
    advance(p); /* consume 'for' */

    Stmt *init = parse_for_init(p);
    if (!init) return NULL;

    Expr *cond = parse_expression(p);
    if (!cond) return NULL;

    if (p->cur.kind != TOK_SEMI) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "';'",
                                           token_describe(p->ps->arena, p->cur), "after condition"));
        return NULL;
    }
    advance(p);

    Stmt *step = parse_for_step(p);
    if (!step) return NULL;

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

    NodeList stmts;
    node_list_init(&stmts);
    while (p->cur.kind != TOK_RBRACE && p->cur.kind != TOK_EOF) {
        Stmt *s = parse_statement(p);
        if (!s) {
            depth_exit(p->ps);
            return NULL;
        }
        node_list_push(p->ps->arena, &stmts, s);
    }

    if (p->cur.kind != TOK_RBRACE) {
        parser_error(p->ps, diag_expected(p->ps->arena, token_span(p->cur), "'}'",
                                           token_describe(p->ps->arena, p->cur), "to close block"));
        depth_exit(p->ps);
        return NULL;
    }
    Token rbrace = p->cur;
    advance(p);

    Stmt *result = stmt_block(p->ps->arena, span_merge(token_span(lbrace), token_span(rbrace)), stmts);
    depth_exit(p->ps);
    return result;
}

Expr *parser_parse_expression(ParserState *ps, Lexer *lx) {
    Parser p;
    p.ps = ps;
    p.lx = lx;
    advance(&p); /* prime the lookahead token */
    return parse_expression(&p);
}

Stmt *parser_parse_block(ParserState *ps, Lexer *lx) {
    Parser p;
    p.ps = ps;
    p.lx = lx;
    advance(&p); /* prime the lookahead token */
    return parse_block(&p);
}
