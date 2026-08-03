/*
 * title.c - terminal window title (OSC 0) for CatShellX
 *
 * Compatible with TerminalX and other xterm-like terminals.  The title
 * is set at every prompt and cleared while a foreground job runs, so
 * the emulator's foreground-process detection can display the running
 * program's name (vim, less, ...) instead of a stale title.
 */

#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int csx_title_active = 0;

int csx_title_enabled(void)
{
    const char *term;

    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO))
        return 0;
    if (csx_bool_var("CSX_TITLE_OFF", 0))
        return 0;
    term = getenv("TERM");
    if (!term || !*term || strcmp(term, "dumb") == 0)
        return 0;
    return 1;
}

void csx_title_set(const char *t)
{
    if (!csx_title_enabled() || !t)
        return;
    fputs("\x1b]0;", stdout);
    fputs(t, stdout);
    fputs("\x07", stdout);
    fflush(stdout);
    csx_title_active = 1;
}

void csx_title_clear(void)
{
    if (!csx_title_active)
        return;
    fputs("\x1b]0;\x07", stdout);
    fflush(stdout);
    csx_title_active = 0;
}
