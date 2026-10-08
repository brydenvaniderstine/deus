#pragma once
#include <stddef.h>
#include <stdint.h>

/* v0 lexer (docs/M2_PLAN.md, pipeline stage 1). Host-side tool, builds
 * with the normal macOS toolchain - not freestanding, not kernel code. */

typedef enum {
    TOK_EOF,

    TOK_IDENT,
    TOK_NUMBER,

    /* Keywords. Built-in type names (u8, bool, ...) are NOT keywords here -
     * they lex as plain TOK_IDENT and are resolved by the typed-IR builder,
     * so the lexer doesn't need to know the full type list. */
    TOK_KW_FN,
    TOK_KW_LET,
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_FOR,
    TOK_KW_WHILE,
    TOK_KW_RETURN,
    TOK_KW_STRUCT,
    TOK_KW_TRUE,
    TOK_KW_FALSE,
    TOK_KW_BREAK,
    TOK_KW_CONTINUE,
    TOK_KW_MANNA,     /* the 'manna v0;' file-header keyword (docs/M2_PLAN.md) */

    /* Punctuation */
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET, /* also used for the [kernel_hook] attribute */
    TOK_COMMA, TOK_SEMI, TOK_COLON, TOK_ARROW, TOK_DOT, TOK_DOTDOT,

    /* Operators */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE,
    TOK_BANG, TOK_ASSIGN, TOK_EQ, TOK_NE,
    TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_AND, TOK_OR,
    TOK_SHL, TOK_SHR,

    /* Errors - the lexer reports and resynchronizes rather than stopping,
     * so one input can surface every error it contains in a single pass. */
    TOK_ERROR_BAD_CHAR,
    TOK_ERROR_UNTERMINATED_COMMENT,
    TOK_ERROR_MALFORMED_NUMBER,
    TOK_ERROR_NUMBER_OVERFLOW,
} TokenKind;

typedef struct {
    TokenKind   kind;
    const char *lexeme;     /* points into the source buffer, not NUL-terminated */
    size_t      lexeme_len;
    int         line;       /* 1-based */
    int         col;        /* 1-based, of the token's first character */
    uint64_t    number_value; /* valid when kind == TOK_NUMBER */
    const char *error_msg;    /* valid when kind is one of the TOK_ERROR_* kinds */
} Token;

/* Deliberately a plain value struct, copyable with a bare `Lexer copy =
 * *lx;` - parser.c's is_top_level_resume_token relies on exactly this to
 * peek several tokens ahead (checking for '[' IDENT ']' 'fn') without
 * disturbing the real parser's position: it copies *lx, calls
 * lexer_next() on the copy, then discards it. This is only safe because
 * every field here is either an immutable borrowed pointer (`src`, never
 * owned or mutated by the lexer - the copy aliases the exact same bytes,
 * which is fine since lexing never writes through it) or a plain scalar.
 * Adding a field that owns memory, points at something the lexer itself
 * mutates, or otherwise makes two copies NOT independently advanceable
 * would silently break that lookahead trick - keep this struct copy-safe
 * by construction, not just true by accident today. */
typedef struct {
    const char *src;
    size_t      len;
    size_t      pos;
    int         line;
    int         col;
} Lexer;

void        lexer_init(Lexer *lx, const char *src, size_t len);
Token       lexer_next(Lexer *lx);
const char *token_kind_name(TokenKind kind);
