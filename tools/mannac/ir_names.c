#include "ir_internal.h"
#include "lexer.h"
#include <ctype.h>
#include <string.h>

/* Naming conventions (typed-IR step 14, docs/M2_PLAN.md "Naming conventions"): the two predicates and the
 * case converter. Pure functions over ASCII identifiers (the lexer admits nothing else): no state, no
 * recursion. The converter splits a name into WORDS and rebuilds it in the target convention.
 *
 * Word splitting: every `_` ends a word and is dropped; inside a run of letters and digits a new word
 * starts (a) before an uppercase letter that follows a lowercase letter or a digit (`fooBar`, `foo2Bar`),
 * and (b) before the LAST uppercase letter of an uppercase run when a lowercase letter follows it
 * (`HTTPServer` -> `HTTP`, `Server`). A digit never starts or ends a word on its own account: `x1y` is
 * one word. */

static bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
static bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool ir_is_snake_case(const char *n) {
    if (!is_lower(n[0])) return false;
    for (const char *p = n + 1; *p; p++) {
        if (is_lower(*p) || is_digit(*p)) continue;
        if (*p == '_' && (is_lower(p[1]) || is_digit(p[1]))) continue; /* a single `_`, followed by a word character */
        return false;
    }
    return true;
}

bool ir_is_pascal_case(const char *n) {
    if (!is_upper(n[0])) return false;
    for (const char *p = n + 1; *p; p++) {
        if (!(is_lower(*p) || is_upper(*p) || is_digit(*p))) return false;
    }
    return true;
}

typedef struct { size_t start, len; } Word;

/* Splits `n` into words (see above); `w` has room for strlen(n) words. Returns the count. */
static size_t split_words(const char *n, Word *w) {
    size_t count = 0, len = strlen(n), i = 0;
    while (i < len) {
        if (n[i] == '_') { i++; continue; }
        size_t start = i;
        for (;;) {
            char c = n[i];
            char next = i + 1 < len ? n[i + 1] : '\0';
            char next2 = i + 2 < len ? n[i + 2] : '\0';
            i++;
            if (next == '\0' || next == '_') break;
            if ((is_lower(c) || is_digit(c)) && is_upper(next)) break;
            if (is_upper(c) && is_upper(next) && is_lower(next2)) break;
        }
        w[count].start = start;
        w[count].len = i - start;
        count++;
    }
    return count;
}

const char *ir_to_snake_case(Arena *a, const char *n) {
    size_t len = strlen(n);
    Word *w = arena_alloc(a, (len + 1) * sizeof(Word), alignof(Word));
    size_t count = split_words(n, w);
    if (count == 0 || is_digit(n[w[0].start])) return NULL; /* empty, or a name that would start with a digit */
    char *out = arena_alloc(a, 2 * len + 1, 1); /* up to one `_` between every two characters */
    size_t o = 0;
    for (size_t k = 0; k < count; k++) {
        if (k) out[o++] = '_';
        for (size_t j = 0; j < w[k].len; j++) out[o++] = (char)tolower((unsigned char)n[w[k].start + j]);
    }
    out[o] = '\0';
    return out;
}

const char *ir_to_pascal_case(Arena *a, const char *n) {
    size_t len = strlen(n);
    Word *w = arena_alloc(a, (len + 1) * sizeof(Word), alignof(Word));
    size_t count = split_words(n, w);
    if (count == 0 || is_digit(n[w[0].start])) return NULL;
    char *out = arena_alloc(a, len + 1, 1);
    size_t o = 0;
    for (size_t k = 0; k < count; k++) {
        for (size_t j = 0; j < w[k].len; j++) {
            char c = n[w[k].start + j];
            out[o++] = j == 0 ? (char)toupper((unsigned char)c) : c; /* the rest as written: acronyms survive */
        }
    }
    out[o] = '\0';
    return out;
}

/* A lexer keyword or the builtin `len`. The keyword set is the lexer's own (asked by lexing the name), so
 * the two cannot drift. */
bool ir_is_reserved_name(const char *n) {
    if (strcmp(n, "len") == 0) return true;
    size_t len = strlen(n);
    Lexer lx;
    lexer_init(&lx, n, len);
    Token t = lexer_next(&lx);
    return t.kind != TOK_IDENT || t.lexeme_len != len;
}
