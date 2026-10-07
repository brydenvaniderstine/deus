#pragma once
#include "ast.h"
#include "lexer.h"
#include "parser_state.h"

/* v0 recursive-descent parser (docs/M2_PLAN.md S4). Built directly on the
 * lexer, threaded through one ParserState (arena/depth/error-cap escape
 * point - see parser_state.h) per file.
 *
 * Step 4 implements primary and postfix expressions only - parser_parse_
 * expression is already the stable public entry point statements/decls
 * will reuse once they exist, but its body currently bottoms out at
 * postfix directly. Step 5 adds the full precedence-climbing chain above
 * it without changing this signature or any caller.
 *
 * Returns NULL on a parse failure that cannot be locally recovered from (a
 * diagnostic is already recorded in ps->diags either way, unless the
 * failure was itself a longjmp to ps->fatal_target, in which case control
 * never returns here at all). There is no resynchronization or
 * placeholder-node logic yet - real error recovery is Step 10's job, not
 * retrofitted here. */
Expr *parser_parse_expression(ParserState *ps, Lexer *lx);
