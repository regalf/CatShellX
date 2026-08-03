/*
 * highlight.c - fish-like syntax highlighting for the line editor
 *
 * Commands are green when found in PATH (or a builtin), red otherwise.
 * Quoted strings are yellow, $VAR references magenta, operators blue,
 * comments dim. Redirection targets keep the default color.
 */

#include "shell.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

static int is_op(char c)
{
    return c == ';' || c == '|' || c == '&' || c == '>' || c == '<';
}

static int has_unquoted_dollar(const char *s)
{
    char q = 0;
    size_t i;
    for (i = 0; s[i]; i++) {
        char c = s[i];
        if (q) {
            if (c == '\\' && q == '"' && s[i + 1]) { i++; continue; }
            if (c == q) q = 0;
            continue;
        }
        if (c == '\'' || c == '"') { q = c; continue; }
        if (c == '\\') { i++; continue; }
        if (c == '$') return 1;
    }
    return 0;
}

static int is_quoted_string(const char *s)
{
    char q = s[0];
    size_t i, n = strlen(s);

    if (n < 2) return 0;
    if (q != '\'' && q != '"') return 0;
    if (s[n - 1] != q) return 0;
    for (i = 1; i < n - 1; i++) {
        if (s[i] == q && (s[i - 1] != '\\' || (i >= 2 && s[i - 2] == '\\')))
            return 0;
    }
    return 1;
}

void csx_highlight(const char *line, strbuf *out)
{
    const char *p = line;
    int cmd_done = 0;

    while (*p) {
        if (*p == ' ' || *p == '\t') {
            sb_putc(out, *p++);
            continue;
        }
        if (*p == '#') {
            sb_puts(out, CSX_C_DIM);
            while (*p) sb_putc(out, *p++);
            sb_puts(out, CSX_C_RESET);
            continue;
        }
        if (is_op(*p)) {
            sb_puts(out, CSX_C_BLUE);
            while (*p && is_op(*p))
                sb_putc(out, *p++);
            sb_puts(out, CSX_C_RESET);
            continue;
        }
        {
            strbuf tok;
            char q = 0;
            int is_cmd = 0;
            const char *t;
            char *validation;
            size_t vlen;

            sb_init(&tok);
            if (!cmd_done) is_cmd = 1;

            while (*p && *p != ' ' && *p != '\t' && !is_op(*p) && *p != '#') {
                char c = *p;
                if (c == '\'' || c == '"') {
                    q = c;
                    sb_putc(&tok, c);
                    p++;
                    while (*p && *p != q) {
                        if (q == '"' && *p == '\\' && p[1]) {
                            sb_putc(&tok, '\\');
                            sb_putc(&tok, p[1]);
                            p += 2;
                            continue;
                        }
                        sb_putc(&tok, *p++);
                    }
                    if (*p == q) { sb_putc(&tok, q); p++; }
                    q = 0;
                    continue;
                }
                if (c == '\\' && p[1]) {
                    sb_putc(&tok, c);
                    sb_putc(&tok, p[1]);
                    p += 2;
                    continue;
                }
                sb_putc(&tok, c);
                p++;
            }

            t = sb_str(&tok);
            if (is_cmd) {
                cmd_done = 1;
                /* strip quotes for PATH validation */
                validation = strdup(t);
                if (validation) {
                    size_t j, w;
                    vlen = strlen(validation);
                    for (j = 0, w = 0; j < vlen; j++) {
                        if (validation[j] != '\'' && validation[j] != '"')
                            validation[w++] = validation[j];
                    }
                    validation[w] = '\0';
                    sb_puts(out, csx_command_exists(validation)
                                  ? CSX_C_GREEN : CSX_C_RED);
                    free(validation);
                } else {
                    sb_puts(out, CSX_C_GREEN);
                }
                sb_puts(out, t);
                sb_puts(out, CSX_C_RESET);
            } else if (has_unquoted_dollar(t)) {
                sb_puts(out, CSX_C_MAGENTA);
                sb_puts(out, t);
                sb_puts(out, CSX_C_RESET);
            } else if (is_quoted_string(t)) {
                sb_puts(out, CSX_C_YELLOW);
                sb_puts(out, t);
                sb_puts(out, CSX_C_RESET);
            } else {
                sb_puts(out, t);
            }
            sb_free(&tok);
        }
    }
}
