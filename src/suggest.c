#include "shell.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Autosuggestion: return the tail of the most recent history entry    */
/* that has `line` as a case-insensitive prefix (fish-style).          */
/* Returns NULL when there is nothing to suggest.                      */
/* ------------------------------------------------------------------ */
const char *csx_suggest(const char *line)
{
    if (!csx_bool_var("CSX_SUGGEST", 1))
        return NULL;
    const char *h = hist_find_prefix(line);
    if (!h)
        return NULL;
    const char *suffix = h + strlen(line);
    return *suffix ? suffix : NULL;
}
