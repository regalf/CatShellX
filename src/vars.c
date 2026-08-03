/*
 * vars.c - shell variables for CatShellX
 *
 * A small table of name/value pairs layered over the environment.
 * `export`ed variables are mirrored into the real environment so that
 * child processes and execvp() see them; unexported ones are visible
 * only to word expansion ($VAR, ${VAR}).
 */

#include "shell.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

typedef struct {
    char *name;
    char *value;
    int exported;
} csx_var;

static csx_var *svars;
static size_t nvars;
static size_t svars_cap;

static csx_var *var_find(const char *name)
{
    size_t i;
    for (i = 0; i < nvars; i++)
        if (strcmp(svars[i].name, name) == 0)
            return &svars[i];
    return NULL;
}

int csx_var_name_ok(const char *name)
{
    const unsigned char *p = (const unsigned char *)name;
    if (!name || !*name)
        return 0;
    if (*p != '_' && !isalpha(*p))
        return 0;
    for (; *p; p++)
        if (*p != '_' && !isalnum(*p))
            return 0;
    return 1;
}

const char *csx_var_get(const char *name)
{
    csx_var *v = var_find(name);
    if (v)
        return v->value;
    return getenv(name);
}

void csx_var_set(const char *name, const char *value, int exported)
{
    csx_var *v;

    if (!csx_var_name_ok(name))
        return;
    v = var_find(name);
    if (!v) {
        if (nvars + 1 > svars_cap) {
            size_t nc = svars_cap ? svars_cap * 2 : 16;
            csx_var *nv = realloc(svars, nc * sizeof(csx_var));
            if (!nv)
                return;
            svars = nv;
            svars_cap = nc;
        }
        v = &svars[nvars++];
        v->name = strdup(name);
        v->value = NULL;
        v->exported = 0;
    }
    free(v->value);
    v->value = value ? strdup(value) : strdup("");
    if (exported) {
        v->exported = 1;
        setenv(name, v->value, 1);
    }
}

void csx_var_unset(const char *name)
{
    csx_var *v = var_find(name);
    size_t i;

    if (!v) {
        unsetenv(name);
        return;
    }
    if (v->exported)
        unsetenv(name);
    free(v->name);
    free(v->value);
    i = (size_t)(v - svars);
    memmove(&svars[i], &svars[i + 1], (nvars - i - 1) * sizeof(csx_var));
    nvars--;
}

/* mode 0: everything (like `set`); mode 1: exported only (like `export`) */
void csx_var_list(FILE *out, int only_exported)
{
    extern char **environ;
    size_t i;

    if (!only_exported) {
        for (i = 0; environ[i]; i++)
            fprintf(out, "%s\n", environ[i]);
    }
    for (i = 0; i < nvars; i++) {
        if (only_exported && !svars[i].exported)
            continue;
        if (!only_exported && svars[i].exported)
            continue; /* already shown via environ */
        fprintf(out, "%s=%s\n", svars[i].name, svars[i].value);
    }
}

/* Keep $PWD/$OLDPWD in sync with reality. */
void csx_var_sync_cwd(void)
{
    char cwd[4096];
    const char *old = csx_var_get("PWD");

    if (getcwd(cwd, sizeof(cwd))) {
        if (old && *old)
            csx_var_set("OLDPWD", old, 1);
        csx_var_set("PWD", cwd, 1);
    }
}

void csx_var_init(void)
{
    csx_var_sync_cwd();
}
