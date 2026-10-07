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
 * Depth bound: only parse_expression and parse_unary's self-recursive
 * prefix case carry depth_enter/depth_exit - not the 10 fixed-pipeline
 * binary-operator levels between logic_or and multiplicative (parsing a
 * plain "a" always walks all 10, a constant cost independent of input,
 * so instrumenting them individually would only inflate the counter
 * without bounding anything new), and not primary/postfix/arg_list/
 * index_or_slice either (an earlier version of this file put hooks there
 * too - removed in the preceding commit once proven redundant and worse:
 * it cost extra depth per level on top of parse_expression's own hook,
 * capping real nesting around 85 levels instead of the intended 256;
 * see that commit message and tests/parser/depth_*).
 *
 * INVARIANT: every recursive cycle in this call graph passes through at
 * least one depth-counted function, so no cycle can spin arbitrarily deep
 * without the shared counter incrementing. The call graph is a strict DAG
 * (expression -> logic_or -> logic_and -> equality -> comparison -> bitor
 * -> bitxor -> bitand -> shift -> additive -> multiplicative -> unary ->
 * postfix -> primary) plus exactly four back-edges, each one a cycle:
 *
 *   1. unary -> unary (self-recursive prefix case, "!!!!x")
 *      Counted by: parse_unary's own hook - this cycle never reaches
 *      parse_expression at all, which is exactly why it needs its own.
 *   2. primary -> expression (the '(' expression ')' case, "(((x)))")
 *      Counted by: parse_expression's hook.
 *   3. primary -> arg_list -> expression (call args, "f(f(f(x)))")
 *      Counted by: parse_expression's hook.
 *   4. postfix -> index_or_slice -> expression (index/slice bounds,
 *      "a[a[a[x]]]")
 *      Counted by: parse_expression's hook.
 *
 * Any FUTURE recursive parse function (types, blocks, statements, once
 * added) must preserve this invariant: either it sits on a cycle that
 * already passes through parse_expression (nothing extra needed), or it
 * introduces a new kind of cycle that doesn't (e.g. a block nesting
 * inside itself without going through an expression), in which case IT
 * needs its own depth_enter/depth_exit, the same way parse_unary does for
 * its self-recursion. The thing to check before adding a new recursive
 * production is not "does every function have a hook" but "does every
 * cycle have at least one."
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
static Expr *parse_logic_or(Parser *p);
static Expr *parse_logic_and(Parser *p);
static Expr *parse_equality(Parser *p);
static Expr *parse_comparison(Parser *p);
static Expr *parse_bitor(Parser *p);
static Expr *parse_bitxor(Parser *p);
static Expr *parse_bitand(Parser *p);
static Expr *parse_shift(Parser *p);
static Expr *parse_additive(Parser *p);
static Expr *parse_multiplicative(Parser *p);
static Expr *parse_unary(Parser *p);
static Expr *parse_postfix(Parser *p);
static Expr *parse_primary(Parser *p);
static bool  parse_arg_list(Parser *p, NodeList *out_args);
static bool  parse_index_or_slice(Parser *p, Expr **out_lo, Expr **out_hi);

static Expr *parse_expression(Parser *p) {
    depth_enter(p->ps, token_span(p->cur));
    Expr *e = parse_logic_or(p);
    depth_exit(p->ps);
    return e;
}

static Expr *parse_logic_or(Parser *p) {
    Expr *lhs = parse_logic_and(p);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_OR) {
        advance(p);
        Expr *rhs = parse_logic_and(p);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), BINOP_OR, lhs, rhs);
    }
    return lhs;
}

static Expr *parse_logic_and(Parser *p) {
    Expr *lhs = parse_equality(p);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_AND) {
        advance(p);
        Expr *rhs = parse_equality(p);
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

static Expr *parse_equality(Parser *p) {
    Expr *lhs = parse_comparison(p);
    if (!lhs) return NULL;
    if (p->cur.kind == TOK_EQ || p->cur.kind == TOK_NE) {
        BinOp op = p->cur.kind == TOK_EQ ? BINOP_EQ : BINOP_NE;
        advance(p);
        Expr *rhs = parse_comparison(p);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), op, lhs, rhs);
        if (is_comparison_or_equality_op(p->cur.kind)) {
            parser_error(p->ps, diag_chained_comparison(token_span(p->cur)));
            return NULL;
        }
    }
    return lhs;
}

static Expr *parse_comparison(Parser *p) {
    Expr *lhs = parse_bitor(p);
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
        Expr *rhs = parse_bitor(p);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), op, lhs, rhs);
        if (is_comparison_op(p->cur.kind)) {
            parser_error(p->ps, diag_chained_comparison(token_span(p->cur)));
            return NULL;
        }
    }
    return lhs;
}

static Expr *parse_bitor(Parser *p) {
    Expr *lhs = parse_bitxor(p);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_PIPE) {
        advance(p);
        Expr *rhs = parse_bitxor(p);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), BINOP_BITOR, lhs, rhs);
    }
    return lhs;
}

static Expr *parse_bitxor(Parser *p) {
    Expr *lhs = parse_bitand(p);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_CARET) {
        advance(p);
        Expr *rhs = parse_bitand(p);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), BINOP_BITXOR, lhs, rhs);
    }
    return lhs;
}

static Expr *parse_bitand(Parser *p) {
    Expr *lhs = parse_shift(p);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_AMP) {
        advance(p);
        Expr *rhs = parse_shift(p);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), BINOP_BITAND, lhs, rhs);
    }
    return lhs;
}

static Expr *parse_shift(Parser *p) {
    Expr *lhs = parse_additive(p);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_SHL || p->cur.kind == TOK_SHR) {
        BinOp op = p->cur.kind == TOK_SHL ? BINOP_SHL : BINOP_SHR;
        advance(p);
        Expr *rhs = parse_additive(p);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), op, lhs, rhs);
    }
    return lhs;
}

static Expr *parse_additive(Parser *p) {
    Expr *lhs = parse_multiplicative(p);
    if (!lhs) return NULL;
    while (p->cur.kind == TOK_PLUS || p->cur.kind == TOK_MINUS) {
        BinOp op = p->cur.kind == TOK_PLUS ? BINOP_ADD : BINOP_SUB;
        advance(p);
        Expr *rhs = parse_multiplicative(p);
        if (!rhs) return NULL;
        lhs = expr_binary(p->ps->arena, span_merge(lhs->span, rhs->span), op, lhs, rhs);
    }
    return lhs;
}

static Expr *parse_multiplicative(Parser *p) {
    Expr *lhs = parse_unary(p);
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

Expr *parser_parse_expression(ParserState *ps, Lexer *lx) {
    Parser p;
    p.ps = ps;
    p.lx = lx;
    advance(&p); /* prime the lookahead token */
    return parse_expression(&p);
}
