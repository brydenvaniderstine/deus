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

/* Parses one '{' statement* '}' block. `statement` recognizes every
 * alternative as of Step 7 (let/if/while/for/return/break/continue/
 * postfix_stmt/expr_stmt) - anything else in statement position is a
 * clean "expected statement, found X" (E0105), not a crash or a
 * silently-accepted no-op. */
Stmt *parser_parse_block(ParserState *ps, Lexer *lx);

/* Parses a whole file: version_decl { top_level_item }. Returns false on
 * any failure (a diagnostic is already recorded either way); *out_items
 * is populated regardless only on success. There is no "Program"/
 * TranslationUnit AST node - version_decl is a compile-time gate, not
 * part of the semantic tree (docs/M2_PLAN.md's AST node types section
 * has no such node), so the top-level items themselves, in order, are
 * the whole program. Naming-convention enforcement and unknown-
 * attribute/duplicate-name checks are explicitly NOT here - both are
 * IR-stage errors (docs/M2_PLAN.md S7 items 7-8), requiring a symbol
 * table this stage doesn't have. */
bool parser_parse_file(ParserState *ps, Lexer *lx, NodeList *out_items);
