#include <string.h>
#include <ctype.h>
#include "lexer.h"

void lexer_init(Lexer *lx, const char *src, size_t len) {
    lx->src = src;
    lx->len = len;
    lx->pos = 0;
    lx->line = 1;
    lx->col = 1;
}

static int at_end(Lexer *lx) {
    return lx->pos >= lx->len;
}

static char peek_at(Lexer *lx, size_t offset) {
    size_t i = lx->pos + offset;
    return (i < lx->len) ? lx->src[i] : '\0';
}

static char peek(Lexer *lx) {
    return peek_at(lx, 0);
}

static char advance(Lexer *lx) {
    char c = lx->src[lx->pos++];
    if (c == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    return c;
}

static int match(Lexer *lx, char expected) {
    if (peek(lx) != expected) return 0;
    advance(lx);
    return 1;
}

static Token make_token(TokenKind kind, const char *start, size_t len, int line, int col) {
    Token t;
    t.kind = kind;
    t.lexeme = start;
    t.lexeme_len = len;
    t.line = line;
    t.col = col;
    t.number_value = 0;
    t.error_msg = NULL;
    return t;
}

static Token make_error(TokenKind kind, const char *start, size_t len, int line, int col, const char *msg) {
    Token t = make_token(kind, start, len, line, col);
    t.error_msg = msg;
    return t;
}

/* Returns 1 and fills *err if an unterminated block comment was hit;
 * otherwise returns 0 once positioned at the next real token (or EOF). */
static int skip_ws_and_comments(Lexer *lx, Token *err) {
    for (;;) {
        char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lx);
            continue;
        }
        if (c == '/' && peek_at(lx, 1) == '/') {
            while (!at_end(lx) && peek(lx) != '\n') advance(lx);
            continue;
        }
        if (c == '/' && peek_at(lx, 1) == '*') {
            /* Block comments do NOT nest (matches C): the first closing
             * delimiter ends it, regardless of any opening delimiter seen
             * since the outermost one. Decided and tested explicitly -
             * tests/lexer/nested_comment.mn - rather than left as
             * incidental behavior. */
            int start_line = lx->line, start_col = lx->col;
            const char *start = lx->src + lx->pos;
            advance(lx); advance(lx); /* consume the opening delimiter */
            while (!at_end(lx) && !(peek(lx) == '*' && peek_at(lx, 1) == '/')) {
                advance(lx);
            }
            if (at_end(lx)) {
                *err = make_error(TOK_ERROR_UNTERMINATED_COMMENT, start,
                                   (size_t)(lx->src + lx->pos - start), start_line, start_col,
                                   "unterminated block comment");
                return 1;
            }
            advance(lx); advance(lx); /* consume closing */
            continue;
        }
        return 0;
    }
}

static int is_ident_start(char c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_ident_cont(char c)  { return isalnum((unsigned char)c) || c == '_'; }
static int is_hex_digit(char c)   { return isxdigit((unsigned char)c); }

static TokenKind keyword_kind(const char *s, size_t len) {
    struct { const char *word; TokenKind kind; } kws[] = {
        {"fn", TOK_KW_FN}, {"let", TOK_KW_LET}, {"if", TOK_KW_IF},
        {"else", TOK_KW_ELSE}, {"for", TOK_KW_FOR}, {"while", TOK_KW_WHILE},
        {"return", TOK_KW_RETURN}, {"struct", TOK_KW_STRUCT},
        {"true", TOK_KW_TRUE}, {"false", TOK_KW_FALSE},
    };
    for (size_t i = 0; i < sizeof(kws) / sizeof(kws[0]); i++) {
        size_t wlen = strlen(kws[i].word);
        if (wlen == len && memcmp(kws[i].word, s, len) == 0) return kws[i].kind;
    }
    return TOK_IDENT;
}

static Token lex_ident(Lexer *lx, int line, int col) {
    const char *start = lx->src + lx->pos;
    while (!at_end(lx) && is_ident_cont(peek(lx))) advance(lx);
    size_t len = (size_t)(lx->src + lx->pos - start);
    return make_token(keyword_kind(start, len), start, len, line, col);
}

/* Consumes a glued-on trailing identifier run (e.g. the "ab" in "12ab", or
 * the "g" in "0x1g") so the whole malformed token is resynchronized past
 * in one step, and the error token's lexeme shows the full offending text. */
static Token lex_malformed_number(Lexer *lx, const char *start, int line, int col, const char *msg) {
    while (!at_end(lx) && is_ident_cont(peek(lx))) advance(lx);
    size_t len = (size_t)(lx->src + lx->pos - start);
    return make_error(TOK_ERROR_MALFORMED_NUMBER, start, len, line, col, msg);
}

static Token lex_number(Lexer *lx, int line, int col) {
    const char *start = lx->src + lx->pos;

    if (peek(lx) == '0' && (peek_at(lx, 1) == 'x' || peek_at(lx, 1) == 'X')) {
        advance(lx); advance(lx); /* consume 0x */
        const char *digits_start = lx->src + lx->pos;
        while (!at_end(lx) && is_hex_digit(peek(lx))) advance(lx);
        size_t ndigits = (size_t)(lx->src + lx->pos - digits_start);
        if (ndigits == 0) {
            return lex_malformed_number(lx, start, line, col,
                                         "0x prefix with no hex digits following it");
        }
        if (!at_end(lx) && is_ident_cont(peek(lx))) {
            return lex_malformed_number(lx, start, line, col,
                                         "invalid character immediately after a hex literal");
        }
        uint64_t value = 0;
        int overflow = 0;
        for (const char *p = digits_start; p < lx->src + lx->pos; p++) {
            char c = *p;
            int d = (c >= '0' && c <= '9') ? c - '0' : (tolower((unsigned char)c) - 'a' + 10);
            if (value > (UINT64_MAX - (uint64_t)d) / 16) overflow = 1;
            value = value * 16 + (uint64_t)d;
        }
        size_t len = (size_t)(lx->src + lx->pos - start);
        if (overflow) {
            return make_error(TOK_ERROR_NUMBER_OVERFLOW, start, len, line, col,
                               "hex literal does not fit in 64 bits");
        }
        Token t = make_token(TOK_NUMBER, start, len, line, col);
        t.number_value = value;
        return t;
    }

    while (!at_end(lx) && isdigit((unsigned char)peek(lx))) advance(lx);
    if (!at_end(lx) && is_ident_cont(peek(lx))) {
        return lex_malformed_number(lx, start, line, col,
                                     "invalid digit or suffix immediately after a number");
    }
    uint64_t value = 0;
    int overflow = 0;
    for (const char *p = start; p < lx->src + lx->pos; p++) {
        uint64_t d = (uint64_t)(*p - '0');
        if (value > (UINT64_MAX - d) / 10) overflow = 1;
        value = value * 10 + d;
    }
    size_t len = (size_t)(lx->src + lx->pos - start);
    if (overflow) {
        return make_error(TOK_ERROR_NUMBER_OVERFLOW, start, len, line, col,
                           "decimal literal does not fit in 64 bits (no silent wraparound)");
    }
    Token t = make_token(TOK_NUMBER, start, len, line, col);
    t.number_value = value;
    return t;
}

Token lexer_next(Lexer *lx) {
    Token err;
    if (skip_ws_and_comments(lx, &err)) return err;

    int line = lx->line, col = lx->col;
    if (at_end(lx)) return make_token(TOK_EOF, lx->src + lx->pos, 0, line, col);

    char c = peek(lx);

    if (is_ident_start(c)) return lex_ident(lx, line, col);
    if (isdigit((unsigned char)c)) return lex_number(lx, line, col);

    const char *start = lx->src + lx->pos;
    advance(lx);

    switch (c) {
        case '(': return make_token(TOK_LPAREN, start, 1, line, col);
        case ')': return make_token(TOK_RPAREN, start, 1, line, col);
        case '{': return make_token(TOK_LBRACE, start, 1, line, col);
        case '}': return make_token(TOK_RBRACE, start, 1, line, col);
        case '[': return make_token(TOK_LBRACKET, start, 1, line, col);
        case ']': return make_token(TOK_RBRACKET, start, 1, line, col);
        case ',': return make_token(TOK_COMMA, start, 1, line, col);
        case ';': return make_token(TOK_SEMI, start, 1, line, col);
        case ':': return make_token(TOK_COLON, start, 1, line, col);
        case '.': return make_token(TOK_DOT, start, 1, line, col);
        case '+': return make_token(TOK_PLUS, start, 1, line, col);
        case '*': return make_token(TOK_STAR, start, 1, line, col);
        case '/': return make_token(TOK_SLASH, start, 1, line, col);
        case '%': return make_token(TOK_PERCENT, start, 1, line, col);
        case '^': return make_token(TOK_CARET, start, 1, line, col);
        case '~': return make_token(TOK_TILDE, start, 1, line, col);

        case '-':
            if (match(lx, '>')) return make_token(TOK_ARROW, start, 2, line, col);
            return make_token(TOK_MINUS, start, 1, line, col);
        case '=':
            if (match(lx, '=')) return make_token(TOK_EQ, start, 2, line, col);
            return make_token(TOK_ASSIGN, start, 1, line, col);
        case '!':
            if (match(lx, '=')) return make_token(TOK_NE, start, 2, line, col);
            return make_token(TOK_BANG, start, 1, line, col);
        case '<':
            if (match(lx, '=')) return make_token(TOK_LE, start, 2, line, col);
            if (match(lx, '<')) return make_token(TOK_SHL, start, 2, line, col);
            return make_token(TOK_LT, start, 1, line, col);
        case '>':
            if (match(lx, '=')) return make_token(TOK_GE, start, 2, line, col);
            if (match(lx, '>')) return make_token(TOK_SHR, start, 2, line, col);
            return make_token(TOK_GT, start, 1, line, col);
        case '&':
            if (match(lx, '&')) return make_token(TOK_AND, start, 2, line, col);
            return make_token(TOK_AMP, start, 1, line, col);
        case '|':
            if (match(lx, '|')) return make_token(TOK_OR, start, 2, line, col);
            return make_token(TOK_PIPE, start, 1, line, col);

        default:
            /* v0 is ASCII-only: bytes >= 0x80 are rejected here, not
             * interpreted as the start of a multi-byte identifier or
             * anything else. */
            if ((unsigned char)c >= 0x80) {
                return make_error(TOK_ERROR_BAD_CHAR, start, 1, line, col,
                                   "non-ASCII byte (v0 source is ASCII-only)");
            }
            return make_error(TOK_ERROR_BAD_CHAR, start, 1, line, col, "unexpected character");
    }
}

const char *token_kind_name(TokenKind kind) {
    switch (kind) {
        case TOK_EOF: return "EOF";
        case TOK_IDENT: return "IDENT";
        case TOK_NUMBER: return "NUMBER";
        case TOK_KW_FN: return "KW_FN";
        case TOK_KW_LET: return "KW_LET";
        case TOK_KW_IF: return "KW_IF";
        case TOK_KW_ELSE: return "KW_ELSE";
        case TOK_KW_FOR: return "KW_FOR";
        case TOK_KW_WHILE: return "KW_WHILE";
        case TOK_KW_RETURN: return "KW_RETURN";
        case TOK_KW_STRUCT: return "KW_STRUCT";
        case TOK_KW_TRUE: return "KW_TRUE";
        case TOK_KW_FALSE: return "KW_FALSE";
        case TOK_LPAREN: return "LPAREN";
        case TOK_RPAREN: return "RPAREN";
        case TOK_LBRACE: return "LBRACE";
        case TOK_RBRACE: return "RBRACE";
        case TOK_LBRACKET: return "LBRACKET";
        case TOK_RBRACKET: return "RBRACKET";
        case TOK_COMMA: return "COMMA";
        case TOK_SEMI: return "SEMI";
        case TOK_COLON: return "COLON";
        case TOK_ARROW: return "ARROW";
        case TOK_DOT: return "DOT";
        case TOK_PLUS: return "PLUS";
        case TOK_MINUS: return "MINUS";
        case TOK_STAR: return "STAR";
        case TOK_SLASH: return "SLASH";
        case TOK_PERCENT: return "PERCENT";
        case TOK_AMP: return "AMP";
        case TOK_PIPE: return "PIPE";
        case TOK_CARET: return "CARET";
        case TOK_TILDE: return "TILDE";
        case TOK_BANG: return "BANG";
        case TOK_ASSIGN: return "ASSIGN";
        case TOK_EQ: return "EQ";
        case TOK_NE: return "NE";
        case TOK_LT: return "LT";
        case TOK_GT: return "GT";
        case TOK_LE: return "LE";
        case TOK_GE: return "GE";
        case TOK_AND: return "AND";
        case TOK_OR: return "OR";
        case TOK_SHL: return "SHL";
        case TOK_SHR: return "SHR";
        case TOK_ERROR_BAD_CHAR: return "ERROR_BAD_CHAR";
        case TOK_ERROR_UNTERMINATED_COMMENT: return "ERROR_UNTERMINATED_COMMENT";
        case TOK_ERROR_MALFORMED_NUMBER: return "ERROR_MALFORMED_NUMBER";
        case TOK_ERROR_NUMBER_OVERFLOW: return "ERROR_NUMBER_OVERFLOW";
    }
    return "UNKNOWN";
}
