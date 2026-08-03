#include "shell.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* strbuf                                                              */
/* ------------------------------------------------------------------ */

void sb_init(strbuf *sb)
{
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_free(strbuf *sb)
{
    free(sb->buf);
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_reset(strbuf *sb)
{
    if (sb->buf)
        sb->buf[0] = '\0';
    sb->len = 0;
}

int sb_ensure(strbuf *sb, size_t extra)
{
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap)
        return 0;
    size_t cap = sb->cap ? sb->cap : 64;
    while (cap < need)
        cap *= 2;
    char *nb = realloc(sb->buf, cap);
    if (!nb)
        return -1;
    sb->buf = nb;
    sb->cap = cap;
    return 0;
}

int sb_putc(strbuf *sb, char c)
{
    if (sb_ensure(sb, 1) != 0)
        return -1;
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
    return 0;
}

int sb_nputs(strbuf *sb, const char *s, size_t n)
{
    if (sb_ensure(sb, n) != 0)
        return -1;
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
    return 0;
}

int sb_puts(strbuf *sb, const char *s)
{
    return sb_nputs(sb, s, strlen(s));
}

int sb_printf(strbuf *sb, const char *fmt, ...)
{
    va_list ap;
    char stackbuf[512];
    va_start(ap, fmt);
    int n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);
    if (n < 0)
        return -1;
    if ((size_t)n < sizeof(stackbuf))
        return sb_nputs(sb, stackbuf, (size_t)n);
    size_t len = (size_t)n;
    if (sb_ensure(sb, len) != 0)
        return -1;
    va_start(ap, fmt);
    vsnprintf(sb->buf + sb->len, len + 1, fmt, ap);
    va_end(ap);
    sb->len += len;
    return 0;
}

char *sb_str(const strbuf *sb)
{
    return sb->buf ? sb->buf : (char *)"";
}

char *sb_detach(strbuf *sb)
{
    char *r = sb->buf ? sb->buf : strdup("");
    sb_init(sb);
    return r;
}

/* ------------------------------------------------------------------ */
/* Portable getline                                                    */
/* ------------------------------------------------------------------ */

ssize_t csx_getline(char **lineptr, size_t *n, FILE *stream)
{
    if (!lineptr || !n || !stream)
        return -1;
    if (*lineptr == NULL) {
        *n = 128;
        *lineptr = malloc(*n);
        if (!*lineptr)
            return -1;
    }
    size_t len = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (len + 2 > *n) {
            size_t ncap = *n * 2;
            char *nb = realloc(*lineptr, ncap);
            if (!nb)
                return -1;
            *lineptr = nb;
            *n = ncap;
        }
        (*lineptr)[len++] = (char)c;
        if (c == '\n')
            break;
    }
    if (len == 0 && c == EOF)
        return -1;
    (*lineptr)[len] = '\0';
    return (ssize_t)len;
}

/* ------------------------------------------------------------------ */
/* Case-insensitive substring search                                   */
/* ------------------------------------------------------------------ */

char *csx_strcasestr(const char *haystack, const char *needle)
{
    if (!haystack || !needle)
        return NULL;
    if (!*needle)
        return (char *)haystack;
    size_t nl = strlen(needle);
    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, nl) == 0)
            return (char *)haystack;
    }
    return NULL;
}
