/*
 * expand.c - word expansion for CatShellX
 *
 * Handles:  ~  $VAR  ${VAR}  $?  $(cmd)  {a,b}  globs (* ? []).
 * Expansion is quote-aware: single quotes protect everything,
 * double quotes allow $ expansion but suppress globbing/splitting,
 * backslash escapes the next character.
 */

#include "shell.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <fnmatch.h>
#include <unistd.h>

#define EXP_MAX_BRACE_DEPTH 16
#define EXP_MAX_WORDS 1024

/* ------------------------------------------------------------------ */
/* Wordlist helpers                                                    */
/* ------------------------------------------------------------------ */

void csx_wl_init(csx_wordlist *wl)
{
    wl->items = NULL;
    wl->count = 0;
    wl->cap = 0;
}

void csx_wl_free(csx_wordlist *wl)
{
    size_t i;
    if (!wl) return;
    for (i = 0; i < wl->count; i++) free(wl->items[i]);
    free(wl->items);
    wl->items = NULL;
    wl->count = 0;
    wl->cap = 0;
}

static void wl_add(csx_wordlist *wl, const char *s)
{
    char *copy;
    if (wl->count >= EXP_MAX_WORDS) return;
    if (wl->count + 1 > wl->cap) {
        size_t nc = wl->cap ? wl->cap * 2 : 8;
        char **ni = realloc(wl->items, nc * sizeof(char *));
        if (!ni) return;
        wl->items = ni;
        wl->cap = nc;
    }
    copy = strdup(s);
    if (copy) wl->items[wl->count++] = copy;
}

/* ------------------------------------------------------------------ */
/* Global state                                                        */
/* ------------------------------------------------------------------ */

int csx_last_status = 0;

/* ------------------------------------------------------------------ */
/* Brace expansion                                                     */
/* ------------------------------------------------------------------ */

static void split_braces(const char *inner, const char *pre, const char *suf,
                         csx_wordlist *wl, int depth);

/*
 * Starting at s[open] == '{', find the matching unquoted '}' and report
 * whether the group contains a top-level comma. Returns 1 if a valid
 * brace group was found (close_out set), 0 otherwise.
 */
static int find_brace(const char *s, size_t open, size_t *close_out)
{
    int depth = 0;
    int has_comma = 0;
    char q = 0;
    size_t i;

    for (i = open; s[i]; i++) {
        char c = s[i];
        if (q) {
            if (c == q) q = 0;
            continue;
        }
        if (c == '\'' || c == '"') { q = c; continue; }
        if (c == '\\') { i++; continue; }
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) {
                if (!has_comma) return 0;
                *close_out = i;
                return 1;
            }
        } else if (c == ',' && depth == 1) {
            has_comma = 1;
        }
    }
    return 0;
}

static void brace_expand(const char *s, csx_wordlist *wl, int depth)
{
    char q = 0;
    size_t i;

    if (depth > EXP_MAX_BRACE_DEPTH) {
        wl_add(wl, s);
        return;
    }

    for (i = 0; s[i]; i++) {
        char c = s[i];
        if (q) {
            if (c == q) q = 0;
            continue;
        }
        if (c == '\'' || c == '"') { q = c; continue; }
        if (c == '\\') { i++; continue; }
        if (c == '{') {
            size_t close;
            strbuf pre, suf, inner;

            if (find_brace(s, i, &close)) {
                sb_init(&pre);
                sb_nputs(&pre, s, i);
                sb_init(&suf);
                sb_puts(&suf, s + close + 1);
                sb_init(&inner);
                sb_nputs(&inner, s + i + 1, close - i - 1);

                split_braces(sb_str(&inner), sb_str(&pre), sb_str(&suf),
                             wl, depth + 1);

                sb_free(&inner);
                sb_free(&suf);
                sb_free(&pre);
                return;
            }
            i++;
            continue;
        }
    }

    wl_add(wl, s);
}

/* Split the contents of a brace group on top-level commas, expanding each
 * pre<item>suf recursively. */
static void split_braces(const char *inner, const char *pre, const char *suf,
                         csx_wordlist *wl, int depth)
{
    int bd = 0;
    char q = 0;
    size_t i;
    strbuf cur;

    sb_init(&cur);
    for (i = 0; inner[i]; i++) {
        char c = inner[i];
        if (q) {
            sb_putc(&cur, c);
            if (c == q) q = 0;
            continue;
        }
        if (c == '\'' || c == '"') { q = c; sb_putc(&cur, c); continue; }
        if (c == '\\') {
            sb_putc(&cur, c);
            if (inner[i + 1]) sb_putc(&cur, inner[++i]);
            continue;
        }
        if (c == '{') { bd++; sb_putc(&cur, c); continue; }
        if (c == '}') { bd--; sb_putc(&cur, c); continue; }
        if (c == ',' && bd == 0) {
            strbuf full;
            sb_init(&full);
            sb_puts(&full, pre);
            sb_puts(&full, sb_str(&cur));
            sb_puts(&full, suf);
            brace_expand(sb_str(&full), wl, depth + 1);
            sb_free(&full);
            sb_reset(&cur);
            continue;
        }
        sb_putc(&cur, c);
    }
    {
        strbuf full;
        sb_init(&full);
        sb_puts(&full, pre);
        sb_puts(&full, sb_str(&cur));
        sb_puts(&full, suf);
        brace_expand(sb_str(&full), wl, depth + 1);
        sb_free(&full);
    }
    sb_free(&cur);
}

/* ------------------------------------------------------------------ */
/* Command substitution ($(cmd))                                       */
/* ------------------------------------------------------------------ */

/* Run cmd, return captured stdout with trailing newlines stripped, or NULL. */
static char *cmd_substitute(const char *cmd)
{
    FILE *fp;
    strbuf out;
    int ch;
    char *res = NULL;

    fp = popen(cmd, "r");
    if (!fp) return NULL;

    sb_init(&out);
    while ((ch = fgetc(fp)) != EOF)
        sb_putc(&out, (char)ch);
    pclose(fp);

    while (out.len > 0 && out.buf[out.len - 1] == '\n')
        out.buf[--out.len] = '\0';

    res = strdup(sb_str(&out));
    sb_free(&out);
    return res;
}

/* ------------------------------------------------------------------ */
/* Core shell expansion                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    strbuf *core;
    unsigned char *flags; /* 1 = quoted (no glob) */
    size_t flen;
    size_t fcap;
} exp;

static void exp_push(exp *e, char c, unsigned char quoted)
{
    if (e->flen + 1 > e->fcap) {
        size_t nc = e->fcap ? e->fcap * 2 : 64;
        unsigned char *nf = realloc(e->flags, nc);
        if (!nf) return;
        e->flags = nf;
        e->fcap = nc;
    }
    sb_putc(e->core, c);
    e->flags[e->flen++] = quoted;
}

static void exp_str(exp *e, const char *s, unsigned char quoted)
{
    size_t i;
    for (i = 0; s[i]; i++)
        exp_push(e, s[i], quoted);
}

/* Append the value of $? / $VAR / ${VAR} (quoted -> no glob). */
static void exp_var(exp *e, const char *name)
{
    strbuf tmp;

    if (name[0] == '?' && name[1] == '\0') {
        sb_init(&tmp);
        sb_printf(&tmp, "%d", csx_last_status);
        exp_str(e, sb_str(&tmp), 1);
        sb_free(&tmp);
        return;
    }
    {
        const char *val = csx_var_get(name);
        if (val) exp_str(e, val, 1);
    }
}

/* Append the result of $(cmd). */
static void exp_subst(exp *e, const char *s, size_t i, size_t *next)
{
    size_t close = i + 1;
    strbuf cmd;
    char *res;

    while (s[close] && s[close] != ')') close++;
    if (s[close] != ')') {
        exp_push(e, '$', 0);
        *next = i + 1;
        return;
    }
    sb_init(&cmd);
    sb_nputs(&cmd, s + i + 2, close - i - 2);
    res = cmd_substitute(sb_str(&cmd));
    if (res) {
        exp_str(e, res, 1);
        free(res);
    }
    sb_free(&cmd);
    *next = close + 1;
}

/* Append the value of a $... reference. */
static void exp_dollar(exp *e, const char *s, size_t i, size_t *next)
{
    if (s[i + 1] == '(') {
        exp_subst(e, s, i, next);
        return;
    }
    if (s[i + 1] == '{') {
        size_t close = i + 2;
        strbuf name;
        while (s[close] && s[close] != '}') close++;
        if (s[close] == '}' && close > i + 2) {
            sb_init(&name);
            sb_nputs(&name, s + i + 2, close - i - 2);
            exp_var(e, sb_str(&name));
            sb_free(&name);
            *next = close + 1;
            return;
        }
        exp_push(e, '$', 0);
        *next = i + 1;
        return;
    }
    if (s[i + 1] == '?') {
        exp_var(e, "?");
        *next = i + 2;
        return;
    }
    if (s[i + 1] == '_' || isalpha((unsigned char)s[i + 1])) {
        size_t j = i + 1;
        strbuf name;
        while (s[j] && (isalnum((unsigned char)s[j]) || s[j] == '_'))
            j++;
        sb_init(&name);
        sb_nputs(&name, s + i + 1, j - i - 1);
        exp_var(e, sb_str(&name));
        sb_free(&name);
        *next = j;
        return;
    }
    exp_push(e, '$', 0);
    *next = i + 1;
}

/* ------------------------------------------------------------------ */
/* Shell expansion walk: produces e->core + quote flags.               */
/* ------------------------------------------------------------------ */

static void shell_expand(const char *s, exp *e)
{
    char q = 0;
    size_t i = 0;

    while (s[i]) {
        char c = s[i];

        if (q == '\'') {
            if (c == '\'') { q = 0; i++; continue; }
            exp_push(e, c, 1);
            i++;
            continue;
        }
        if (q == '"') {
            if (c == '"') { q = 0; i++; continue; }
            if (c == '\\' && (s[i + 1] == '"' || s[i + 1] == '\\' ||
                              s[i + 1] == '$')) {
                exp_push(e, s[i + 1], 1);
                i += 2;
                continue;
            }
            if (c == '$') {
                exp_dollar(e, s, i, &i);
                continue;
            }
            exp_push(e, c, 1);
            i++;
            continue;
        }
        /* unquoted */
        if (c == '\'') { q = '\''; i++; continue; }
        if (c == '"') { q = '"'; i++; continue; }
        if (c == '\\') {
            if (s[i + 1]) { exp_push(e, s[i + 1], 1); i += 2; }
            else i++;
            continue;
        }
        if (c == '~' && e->core->len == 0 &&
            (s[i + 1] == '/' || s[i + 1] == '\0')) {
            const char *home = getenv("HOME");
            if (home) exp_str(e, home, 0);
            i++;
            continue;
        }
        if (c == '$') {
            exp_dollar(e, s, i, &i);
            continue;
        }
        exp_push(e, c, 0);
        i++;
    }
}

/* ------------------------------------------------------------------ */
/* Glob expansion                                                      */
/* ------------------------------------------------------------------ */

static int has_glob(const exp *e)
{
    size_t i;
    for (i = 0; i < e->flen; i++)
        if (!e->flags[i] &&
            (e->core->buf[i] == '*' || e->core->buf[i] == '?' ||
             e->core->buf[i] == '['))
            return 1;
    return 0;
}

static int name_cmp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Expand globs in the core; fall back to the literal core if no match. */
static void glob_expand(const exp *e, csx_wordlist *wl)
{
    const char *pat = sb_str(e->core);
    const char *slash;
    char dir[1024];
    char pattern[1024];
    DIR *d;
    struct dirent *de;
    char **m = NULL;
    size_t nm = 0, cm = 0;
    size_t i;
    int prefix;

    if (!has_glob(e)) {
        wl_add(wl, pat);
        return;
    }

    slash = strrchr(pat, '/');
    if (slash) {
        size_t dlen = (size_t)(slash - pat);
        if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
        memcpy(dir, pat, dlen);
        dir[dlen] = '\0';
        strncpy(pattern, slash + 1, sizeof(pattern) - 1);
        pattern[sizeof(pattern) - 1] = '\0';
        prefix = 1;
    } else {
        dir[0] = '\0';
        strncpy(pattern, pat, sizeof(pattern) - 1);
        pattern[sizeof(pattern) - 1] = '\0';
        prefix = 0;
    }

    d = opendir(dir[0] ? dir : ".");
    if (!d) {
        wl_add(wl, pat);
        return;
    }
    while ((de = readdir(d)) != NULL) {
        char full[2048];
        if (de->d_name[0] == '.' && pattern[0] != '.')
            continue;
        if (fnmatch(pattern, de->d_name, 0) != 0)
            continue;
        if (prefix) {
            if (dir[0] && dir[1]) {
                size_t n = strlen(dir) + 1 + strlen(de->d_name);
                if (n >= sizeof(full)) continue;
                snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
            } else {
                if (dir[0] == '/') {
                    size_t n = 1 + strlen(de->d_name);
                    if (n >= sizeof(full)) continue;
                    snprintf(full, sizeof(full), "/%s", de->d_name);
                } else {
                    size_t n = strlen(de->d_name);
                    if (n >= sizeof(full)) continue;
                    snprintf(full, sizeof(full), "%s", de->d_name);
                }
            }
        } else {
            size_t n = strlen(de->d_name);
            if (n >= sizeof(full)) continue;
            snprintf(full, sizeof(full), "%s", de->d_name);
        }
        if (nm + 1 > cm) {
            size_t nc = cm ? cm * 2 : 16;
            char **ni = realloc(m, nc * sizeof(char *));
            if (!ni) break;
            m = ni;
            cm = nc;
        }
        m[nm] = strdup(full);
        if (m[nm]) nm++;
    }
    closedir(d);

    if (nm == 0) {
        wl_add(wl, pat);
    } else {
        qsort(m, nm, sizeof(char *), name_cmp);
        for (i = 0; i < nm; i++) {
            wl_add(wl, m[i]);
            free(m[i]);
        }
    }
    free(m);
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                  */
/* ------------------------------------------------------------------ */

/* Expand a single raw token into zero or more words. */
int csx_expand_word(const char *raw, csx_wordlist *wl)
{
    csx_wordlist braces;
    size_t i;

    csx_wl_init(wl);

    /* 1. brace expansion (produces 1..N raw words) */
    csx_wl_init(&braces);
    brace_expand(raw, &braces, 0);

    /* 2. for each braced word: shell expansion then globbing */
    for (i = 0; i < braces.count; i++) {
        strbuf core;
        exp cur;

        sb_init(&core);
        cur.core = &core;
        cur.flags = NULL;
        cur.flen = 0;
        cur.fcap = 0;

        shell_expand(braces.items[i], &cur);
        glob_expand(&cur, wl);

        free(cur.flags);
        sb_free(&core);
    }
    csx_wl_free(&braces);

    if (wl->count == 0) {
        /* e.g. $UNSET alone produced nothing */
        csx_wl_free(wl);
        return 0;
    }
    return (int)wl->count;
}
