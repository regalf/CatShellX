/*
 * alias.c - aliases for CatShellX
 *
 * Aliases are name/value pairs expanded on the first word of a command.
 * The value is split on whitespace and inserted before the remaining
 * arguments, so `alias ll='ls -l'` followed by `ll -a` runs `ls -l -a`.
 */

#include "shell.h"
#include "parser.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    char *name;
    char *value;
} csx_alias;

static csx_alias *aliases;
static size_t naliases;
static size_t aliases_cap;

static csx_alias *alias_find(const char *name)
{
    size_t i;
    for (i = 0; i < naliases; i++)
        if (strcmp(aliases[i].name, name) == 0)
            return &aliases[i];
    return NULL;
}

void csx_alias_set(const char *name, const char *value)
{
    csx_alias *a;

    if (!csx_var_name_ok(name))
        return;
    a = alias_find(name);
    if (!a) {
        if (naliases + 1 > aliases_cap) {
            size_t nc = aliases_cap ? aliases_cap * 2 : 16;
            csx_alias *na = realloc(aliases, nc * sizeof(csx_alias));
            if (!na)
                return;
            aliases = na;
            aliases_cap = nc;
        }
        a = &aliases[naliases++];
        a->name = strdup(name);
        a->value = NULL;
    }
    free(a->value);
    a->value = value ? strdup(value) : strdup("");
}

const char *csx_alias_get(const char *name)
{
    csx_alias *a = alias_find(name);
    return a ? a->value : NULL;
}

void csx_alias_unset(const char *name)
{
    csx_alias *a = alias_find(name);
    size_t i;

    if (!a)
        return;
    free(a->name);
    free(a->value);
    i = (size_t)(a - aliases);
    memmove(&aliases[i], &aliases[i + 1],
            (naliases - i - 1) * sizeof(csx_alias));
    naliases--;
}

int csx_alias_list(FILE *out)
{
    size_t i;
    for (i = 0; i < naliases; i++)
        fprintf(out, "alias %s='%s'\n", aliases[i].name, aliases[i].value);
    return (int)naliases;
}

size_t csx_alias_count(void)
{
    return naliases;
}

const char *csx_alias_name(size_t i)
{
    return i < naliases ? aliases[i].name : NULL;
}

/*
 * Expand the first word of a command array in place.  `words` is a
 * NULL-terminated argv-style array of up to CSX_MAX_WORDS entries whose
 * strings are heap-allocated; *nwords is the live word count and is
 * updated to match.  The alias value is split on whitespace and
 * recursively resolved (safety-limited).  Returns 1 if any expansion
 * happened, 0 otherwise.
 */
static int expand_once(char **words, int *nwords, int depth)
{
    const char *val;
    char *copy;
    char *save = NULL;
    char *tok;
    int ntok = 0;
    int nrest = *nwords;
    int k;
    char *newargs[CSX_MAX_WORDS];

    if (depth > 16 || !words[0] || *nwords <= 0 || *nwords > CSX_MAX_WORDS)
        return 0;

    val = csx_alias_get(words[0]);
    if (!val || !*val)
        return 0;

    copy = strdup(val);
    if (!copy)
        return 0;

    tok = strtok_r(copy, " \t", &save);
    while (tok && ntok < CSX_MAX_WORDS) {
        newargs[ntok++] = tok;
        tok = strtok_r(NULL, " \t", &save);
    }

    if (ntok == 0) {
        free(copy);
        return 0;
    }

    if (ntok + nrest - 1 >= CSX_MAX_WORDS) {
        free(copy);
        return 0;
    }

    for (k = 1; k < nrest; k++)
        newargs[ntok + k - 1] = words[k];

    /* Free the old first word; tail args are reused, alias tokens strdup'd
     * because they live inside `copy`. */
    free(words[0]);
    for (k = 0; k < ntok + nrest - 1; k++) {
        if (k < ntok)
            words[k] = strdup(newargs[k]);
        else
            words[k] = newargs[k];
    }
    words[ntok + nrest - 1] = NULL;
    *nwords = ntok + nrest - 1;

    free(copy);
    return 1 + expand_once(words, nwords, depth + 1);
}

int csx_alias_expand_cmd(char **words, int *nwords)
{
    int r = expand_once(words, nwords, 0);
    return r > 0;
}

void csx_alias_shutdown(void)
{
    size_t i;
    for (i = 0; i < naliases; i++) {
        free(aliases[i].name);
        free(aliases[i].value);
    }
    free(aliases);
    aliases = NULL;
    naliases = 0;
    aliases_cap = 0;
}
