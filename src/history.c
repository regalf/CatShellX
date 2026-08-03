#include "shell.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define HIST_MAX 1000

static char *hist[HIST_MAX];
static int hist_count = 0;
static int hist_nav = 0;
static int hist_search_pos = 0;

static const char *hist_path(void)
{
    static char path[4096];
    const char *home = getenv("HOME");
    if (!home)
        home = ".";
    snprintf(path, sizeof(path), "%s/.catshellx_history", home);
    return path;
}

void hist_load(void)
{
    FILE *f = fopen(hist_path(), "r");
    if (!f)
        return;
    char *line = NULL;
    size_t cap = 0;
    while (csx_getline(&line, &cap, f) >= 0) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n == 0)
            continue;
        if (hist_count < HIST_MAX)
            hist[hist_count++] = strdup(line);
    }
    free(line);
    fclose(f);
    hist_nav = hist_count;
}

void hist_add(const char *line)
{
    if (!line || !*line)
        return;
    if (hist_count > 0 && strcmp(hist[hist_count - 1], line) == 0)
        return;
    if (hist_count >= HIST_MAX) {
        free(hist[0]);
        memmove(hist, hist + 1, (HIST_MAX - 1) * sizeof(char *));
        hist_count = HIST_MAX - 1;
    }
    hist[hist_count++] = strdup(line);
    hist_nav = hist_count;
}

void hist_save(void)
{
    FILE *f = fopen(hist_path(), "w");
    if (!f)
        return;
    int i;
    for (i = 0; i < hist_count; i++)
        fprintf(f, "%s\n", hist[i]);
    fclose(f);
}

void hist_nav_reset(void)
{
    hist_nav = hist_count;
}

const char *hist_nav_prev(void)
{
    if (hist_count == 0)
        return NULL;
    if (hist_nav > 0)
        hist_nav--;
    return hist[hist_nav];
}

const char *hist_nav_next(void)
{
    if (hist_nav < hist_count)
        hist_nav++;
    if (hist_nav >= hist_count)
        return NULL;
    return hist[hist_nav];
}

static int hist_contains(const char *h, const char *q)
{
    return h && q && *q && csx_strcasestr(h, q) != NULL;
}

const char *hist_search(const char *q)
{
    int i;
    for (i = hist_count - 1; i >= 0; i--) {
        if (hist_contains(hist[i], q)) {
            hist_search_pos = i;
            return hist[i];
        }
    }
    return NULL;
}

const char *hist_search_older(const char *q)
{
    int i;
    for (i = hist_search_pos - 1; i >= 0; i--) {
        if (hist_contains(hist[i], q)) {
            hist_search_pos = i;
            return hist[i];
        }
    }
    return NULL;
}

const char *hist_find_prefix(const char *p)
{
    if (!p || !*p)
        return NULL;
    size_t plen = strlen(p);
    int i;
    for (i = hist_count - 1; i >= 0; i--) {
        if (strlen(hist[i]) > plen &&
            strncasecmp(hist[i], p, plen) == 0)
            return hist[i];
    }
    return NULL;
}

void hist_shutdown(void)
{
    int i;
    for (i = 0; i < hist_count; i++)
        free(hist[i]);
    hist_count = 0;
    hist_nav = 0;
    hist_search_pos = 0;
}
