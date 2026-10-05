#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"

/* Reads a .mn file and dumps one line per token to stdout:
 *   line:col KIND lexeme                  (most tokens)
 *   line:col NUMBER lexeme value=N         (numbers)
 *   line:col KIND lexeme msg="..."         (errors)
 * Used directly by the golden tests in tests/lexer/ (make test-lexer). */

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "mannac: cannot open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fprintf(stderr, "mannac: out of memory\n");
        exit(1);
    }
    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);
    *out_len = n;
    return buf;
}

static void print_lexeme(const Token *t) {
    fwrite(t->lexeme, 1, t->lexeme_len, stdout);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <file.mn>\n", argv[0]);
        return 1;
    }

    size_t len;
    char *src = read_file(argv[1], &len);

    Lexer lx;
    lexer_init(&lx, src, len);

    for (;;) {
        Token t = lexer_next(&lx);

        printf("%d:%d %s ", t.line, t.col, token_kind_name(t.kind));
        print_lexeme(&t);

        if (t.kind == TOK_NUMBER) {
            printf(" value=%llu", (unsigned long long)t.number_value);
        } else if (t.error_msg) {
            printf(" msg=\"%s\"", t.error_msg);
        }
        printf("\n");

        if (t.kind == TOK_EOF) break;
    }

    free(src);
    return 0;
}
