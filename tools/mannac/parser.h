#pragma once
#include "ast.h"
#include "lexer.h"
#include "parser_state.h"

/* v0 recursive-descent parser (docs/M2_PLAN.md S4). Built directly on the
 * lexer, threaded through one ParserState (arena/depth/error-cap escape
 * point - see parser_state.h) per file.
 *
 * Steps 4-5 built the expression grammar (primary/postfix, then the full
 * precedence chain) behind parser_parse_expression. Step 6 adds block-
 * level statements (let/return/break/continue, and the block wrapper
 * itself) behind parser_parse_block - the statement-level counterpart,
 * reused as-is once if/while/for/postfix_stmt/expr_stmt (Step 7) and
 * top-level items (Step 8) extend what `statement` and `type` recognize.
 *
 * Returns NULL on a parse failure that cannot be locally recovered from (a
 * diagnostic is already recorded in ps->diags either way, unless the
 * failure was itself a longjmp to ps->fatal_target, in which case control
 * never returns here at all). There is no resynchronization or
 * placeholder-node logic yet - real error recovery is Step 10's job, not
 * retrofitted here. */
Expr *parser_parse_expression(ParserState *ps, Lexer *lx);

/* Parses one '{' statement* '}' block. Step 6's `statement` recognizes
 * only let/return/break/continue (if/while/for/postfix_stmt/expr_stmt are
 * Step 7) - anything else in statement position is a clean "expected
 * statement, found X" (E0105), not a crash or a silently-accepted no-op. */
Stmt *parser_parse_block(ParserState *ps, Lexer *lx);
