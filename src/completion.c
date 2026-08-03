#include "shell.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_CANDS 256
#define MAXLEN    1024

void csx_completion_free(csx_completion *c)
{
    size_t i;
    for (i = 0; i < c->count; i++)
        free(c->items[i]);
    free(c->items);
    c->items = NULL;
    c->count = 0;
}

static void add_cand(csx_completion *c, const char *s)
{
    if (c->count >= MAX_CANDS)
        return;
    c->items[c->count++] = strdup(s);
}

static int cmp_cand(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void dedupe(csx_completion *c)
{
    size_t w = 0;
    size_t i;
    for (i = 0; i < c->count; i++) {
        if (w > 0 && strcmp(c->items[w - 1], c->items[i]) == 0) {
            free(c->items[i]);
            continue;
        }
        c->items[w++] = c->items[i];
    }
    c->count = w;
}

static int starts_with(const char *s, const char *prefix)
{
    return strncasecmp(s, prefix, strlen(prefix)) == 0;
}

/* ------------------------------------------------------------------ */
/* command completion: builtins + executables in $PATH                 */
/* ------------------------------------------------------------------ */
static void complete_command(csx_completion *c, const char *prefix)
{
    size_t plen = strlen(prefix);
    const char *const *bi = csx_builtin_list();
    size_t i;
    for (i = 0; bi[i]; i++) {
        if (strncasecmp(bi[i], prefix, plen) == 0)
            add_cand(c, bi[i]);
    }

    for (i = 0; i < csx_alias_count(); i++) {
        const char *name = csx_alias_name(i);
        if (name && strncasecmp(name, prefix, plen) == 0)
            add_cand(c, name);
    }

    const char *path = getenv("PATH");
    if (path) {
        char *copy = strdup(path);
        char *save = NULL;
        char *d = strtok_r(copy, ":", &save);
        while (d) {
            DIR *dir = opendir(d);
            if (dir) {
                struct dirent *de;
                while ((de = readdir(dir)) != NULL) {
                    if (de->d_name[0] == '.')
                        continue;
                    if (!starts_with(de->d_name, prefix))
                        continue;
                    char full[MAXLEN];
                    int n = snprintf(full, sizeof(full), "%s/%s", d, de->d_name);
                    if (n < 0 || (size_t)n >= sizeof(full))
                        continue;
                    if (access(full, X_OK) == 0)
                        add_cand(c, de->d_name);
                }
                closedir(dir);
            }
            d = strtok_r(NULL, ":", &save);
        }
        free(copy);
    }

    qsort(c->items, c->count, sizeof(char *), cmp_cand);
    dedupe(c);

    if (c->count == 1) {
        size_t n = strlen(c->items[0]);
        char *ext = realloc(c->items[0], n + 2);
        if (ext) {
            ext[n] = ' ';
            ext[n + 1] = '\0';
            c->items[0] = ext;
        }
    }
}

/* ------------------------------------------------------------------ */
/* path completion: files and directories                              */
/* ------------------------------------------------------------------ */
static void complete_path(csx_completion *c, const char *word)
{
    char dir[MAXLEN];
    char outdir[MAXLEN];
    char namepref[MAXLEN];

    const char *slash = strrchr(word, '/');
    if (slash) {
        size_t dn = (size_t)(slash - word) + 1;
        if (dn >= MAXLEN)
            dn = MAXLEN - 1;
        memcpy(outdir, word, dn);
        outdir[dn] = '\0';
        memcpy(dir, word, dn);
        dir[dn] = '\0';
        strncpy(namepref, slash + 1, MAXLEN - 1);
        namepref[MAXLEN - 1] = '\0';
    } else {
        outdir[0] = '\0';
        dir[0] = '\0';
        strncpy(namepref, word, MAXLEN - 1);
        namepref[MAXLEN - 1] = '\0';
    }

    if (dir[0] == '~') {
        const char *home = getenv("HOME");
        char expanded[MAXLEN];
        if (home) {
            if (dir[1] == '/' || dir[1] == '\0')
                snprintf(expanded, sizeof(expanded), "%s%s", home, dir + 1);
            else
                snprintf(expanded, sizeof(expanded), "%s", dir);
            strncpy(dir, expanded, MAXLEN - 1);
            dir[MAXLEN - 1] = '\0';
        }
    }
    if (dir[0] == '\0')
        strcpy(dir, ".");

    size_t plen = strlen(namepref);
    DIR *d = opendir(dir);
    if (!d)
        return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (name[0] == '.' && (name[1] == '\0' || name[1] == '.'))
            continue;
        if (name[0] == '.' && plen > 0 && namepref[0] != '.')
            continue;
        if (plen > 0 && !starts_with(name, namepref))
            continue;

        char cand[MAXLEN];
        int nc = snprintf(cand, sizeof(cand), "%s%s", outdir, name);
        if (nc < 0 || (size_t)nc >= sizeof(cand))
            continue;
        char full[MAXLEN];
        int nf = snprintf(full, sizeof(full), "%s/%s", dir, name);
        if (nf < 0 || (size_t)nf >= sizeof(full))
            continue;
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
            strncat(cand, "/", sizeof(cand) - strlen(cand) - 1);
        add_cand(c, cand);
    }
    closedir(d);
}

/* ------------------------------------------------------------------ */
/* entry point                                                         */
/* ------------------------------------------------------------------ */
int csx_complete_line(const char *line, size_t pos, csx_completion *out)
{
    out->items = calloc(MAX_CANDS, sizeof(char *));
    out->count = 0;
    out->base = 0;
    if (!out->items)
        return 0;

    size_t ws = pos;
    while (ws > 0 && line[ws - 1] != ' ' && line[ws - 1] != '\t')
        ws--;
    out->base = ws;

    char word[MAXLEN];
    size_t wl = pos - ws;
    if (wl >= MAXLEN)
        wl = MAXLEN - 1;
    memcpy(word, line + ws, wl);
    word[wl] = '\0';

    size_t j = 0;
    while (j < ws && (line[j] == ' ' || line[j] == '\t'))
        j++;
    if (j == ws)
        complete_command(out, word);
    else
        complete_path(out, word);

    qsort(out->items, out->count, sizeof(char *), cmp_cand);
    return 1;
}
