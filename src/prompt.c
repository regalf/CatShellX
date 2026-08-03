#include "shell.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * Render a fish-like prompt into out, returning its display width
 * (prompt_width drives cursor positioning in the line editor).
 *
 * If $CSX_PROMPT is set it is used instead, with escapes:
 *   \u user   \h host (short)   \s shell name   \w cwd (~ for $HOME)
 *   \W basename of cwd   \d date   \t HH:MM:SS   \n newline
 *   \$ literal $   \\ literal \   \e ESC byte (for color codes)
 * Unknown escapes print the escaped character literally.
 */

static const char *csx_username(void)
{
    const char *u = getenv("USER");
    if (!u)
        u = getenv("USERNAME");
    return u ? u : "?";
}

static const char *csx_hostname(void)
{
    static char host[256];
    if (gethostname(host, sizeof(host)) != 0)
        strcpy(host, "?");
    char *dot = strchr(host, '.');
    if (dot)
        *dot = '\0';
    return host;
}

static const char *csx_cwd_short(char *buf, size_t sz)
{
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        strncpy(buf, "?", sz - 1);
        buf[sz - 1] = '\0';
        return buf;
    }
    const char *home = getenv("HOME");
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        if (cwd[strlen(home)] == '\0') {
            strncpy(buf, "~", sz - 1);
            buf[sz - 1] = '\0';
            return buf;
        }
        if (cwd[strlen(home)] == '/') {
            snprintf(buf, sz, "~%s", cwd + strlen(home));
            return buf;
        }
    }
    strncpy(buf, cwd, sz - 1);
    buf[sz - 1] = '\0';
    return buf;
}

static const char *csx_cwd_base(char *buf, size_t sz)
{
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        strncpy(buf, "?", sz - 1);
        buf[sz - 1] = '\0';
        return buf;
    }
    const char *b = strrchr(cwd, '/');
    strncpy(buf, b ? b + 1 : cwd, sz - 1);
    buf[sz - 1] = '\0';
    return buf;
}

static void render_custom(strbuf *out, const char *p)
{
    size_t i = 0;
    while (p[i]) {
        if (p[i] == '\\' && p[i + 1]) {
            char e = p[i + 1];
            char buf[4096];
            char tbuf[64];
            time_t now;
            struct tm *lt;
            switch (e) {
            case 'u': sb_puts(out, csx_username()); break;
            case 'h': sb_puts(out, csx_hostname()); break;
            case 's': sb_puts(out, "catshellx"); break;
            case 'w': sb_puts(out, csx_cwd_short(buf, sizeof(buf))); break;
            case 'W': sb_puts(out, csx_cwd_base(buf, sizeof(buf))); break;
            case 'd':
                now = time(NULL);
                lt = localtime(&now);
                if (lt && strftime(tbuf, sizeof(tbuf), "%a %b %d", lt))
                    sb_puts(out, tbuf);
                break;
            case 't':
                now = time(NULL);
                lt = localtime(&now);
                if (lt && strftime(tbuf, sizeof(tbuf), "%H:%M:%S", lt))
                    sb_puts(out, tbuf);
                break;
            case 'n': sb_putc(out, '\n'); break;
            case '$': sb_putc(out, '$'); break;
            case 'e': sb_putc(out, 0x1b); break;
            default: sb_putc(out, e); break;
            }
            i += 2;
            continue;
        }
        sb_putc(out, p[i]);
        i++;
    }
}

/* Visible width: ANSI CSI sequences and UTF-8 continuation bytes excluded;
 * a newline resets the running column count. */
static size_t visible_width(const char *s)
{
    size_t w = 0;
    size_t i = 0;
    while (s[i]) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0x1b && s[i + 1] == '[') {
            i += 2;
            while (s[i] && !(s[i] >= 0x40 && s[i] <= 0x7e))
                i++;
            if (s[i])
                i++;
            continue;
        }
        if (c == '\n') {
            w = 0;
            i++;
            continue;
        }
        if ((c & 0xc0) == 0x80) {
            i++;
            continue;
        }
        w++;
        i++;
    }
    return w;
}

static size_t default_prompt(strbuf *out)
{
    char basebuf[4096];
    const char *base = csx_cwd_short(basebuf, sizeof(basebuf));
    const char *user = csx_username();
    const char *host = csx_hostname();

    sb_printf(out, CSX_C_GREEN "%s@%s" CSX_C_RESET, user, host);
    sb_printf(out, " " CSX_C_CYAN "%s" CSX_C_RESET, base);
    sb_puts(out, " " CSX_C_BYELLOW "\xe2\x9d\xaf" CSX_C_RESET " ");

    return strlen(user) + 1 + strlen(host) + 1 + strlen(base) + 1 + 1 + 1;
}

size_t csx_prompt(strbuf *out)
{
    const char *custom = csx_var_get("CSX_PROMPT");
    if (custom && *custom) {
        render_custom(out, custom);
        return visible_width(sb_str(out));
    }
    return default_prompt(out);
}
