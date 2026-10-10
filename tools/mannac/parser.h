#pragma once
#include "ast.h"
#include "lexer.h"
#include "parser_state.h"

/* v0 recursive-descent parser (docs/M2_PLAN.md S4). Built directly on the
 * lexer, threaded through one ParserState (arena/depth/error-cap escape
 * point - see parser_state.h) per file.
 *
 * Three entry points, built up in steps: parser_parse_expression (Steps
 * 4-5: primary/postfix, then the full precedence chain), parser_parse_block
 * (Steps 6-7: every statement kind, plus the block wrapper), and
 * parser_parse_file (Step 8: version_decl and top-level items). Step 10b
 * adds Wirth-style follow-set error recovery at the statement,
 * block, header, and top-level-item granularities (see parser.c) - an
 * expression is not one of those granularities, so this function itself
 * still has no recovery of its own: a malformed expression still returns
 * NULL here exactly as before, with the nearest enclosing statement or
 * top-level item being where recovery actually happens (it drops that
 * whole statement/item, not just the bad sub-expression within it).
 *
 * Returns NULL on a parse failure that cannot be locally recovered from (a
 * diagnostic is already recorded in ps->diags either way, unless the
 * failure was itself a longjmp to ps->fatal_target, in which case control
 * never returns here at all). */
Expr *parser_parse_expression(ParserState *ps, Lexer *lx);

/* Computes a token's full source span (start AND end line/col) from its
 * line/col/lexeme_len - exposed for parser_fixes_test.c (not just parser.c's
 * internal use) because it's a pure, self-contained function worth
 * verifying in isolation: lexeme_len is a raw byte count, so a token
 * whose lexeme spans more than one line (the only one today is
 * TOK_ERROR_UNTERMINATED_COMMENT - see lexer.c's skip_ws_and_comments)
 * needs its embedded newlines accounted for, not just added onto the
 * start column. */
Span token_span(Token t);

/* Parses one '{' statement* '}' block. `statement` recognizes every
 * alternative as of Step 7 (let/if/while/for/return/break/continue/
 * postfix_stmt/expr_stmt) - anything else in statement position is a
 * clean "expected statement, found X" (E0105), not a crash or a
 * silently-accepted no-op. As of Step 10b, a malformed statement doesn't
 * abort the whole block: it's dropped (no placeholder node) and parsing
 * resumes at the next token owned by any currently-active recovery level
 * (block's own '}', statement's ';'/start-keywords, an enclosing header's
 * '{', or a top-level start) - see parse_block/sync_recover_statement/
 * sync_recover_block in parser.c, and docs/M2_PLAN.md's "Error recovery"
 * table. This function still returns NULL if the block's own
 * delimiters are unrecoverably missing (no opening '{', or EOF reached
 * before the closing '}'). A `fn`/`struct` where a statement or '}' was
 * expected, with a top level above the block, ends the block instead: the
 * statements so far are kept and the token is left for the top level - recovering from a missing/mismatched
 * delimiter itself is a different problem than recovering from bad
 * content between two well-placed ones, and isn't handled here. */
Stmt *parser_parse_block(ParserState *ps, Lexer *lx);

/* Parses a whole file: version_decl { top_level_item }. There is no
 * "Program"/TranslationUnit AST node - version_decl is a compile-time
 * gate, not part of the semantic tree (docs/M2_PLAN.md's AST node types
 * section has no such node), so the top-level items themselves, in
 * order, are the whole program. Naming-convention enforcement and
 * unknown-attribute/duplicate-name checks are explicitly NOT here - both
 * are IR-stage errors (docs/M2_PLAN.md S7 items 7-8), requiring a symbol
 * table this stage doesn't have.
 *
 * As of Step 10b, a malformed top-level item doesn't abort the whole
 * file: it's dropped (no placeholder node) and parsing resumes at the
 * next token this level owns (fn/struct/an attribute's '[') - see
 * parser_parse_file/sync_recover_top_level in parser.c, and
 * docs/M2_PLAN.md's "Error recovery" table. Returns false only if
 * version_decl itself fails (unrecoverable by design - there's no
 * sensible "resume" from a missing/wrong file header); *out_items is
 * populated and the function returns true otherwise, even if one or
 * more items were dropped along the way (check ps->diags.len to know
 * whether that happened). */
bool parser_parse_file(ParserState *ps, Lexer *lx, NodeList *out_items);
