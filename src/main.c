#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static void usage(FILE *out)
{
    fprintf(out, "usage: %s [-c command] [-h] [-v]\n", "catshellx");
    fprintf(out, "  -c command   run the given command and exit\n");
    fprintf(out, "  -h           show this help\n");
    fprintf(out, "  -v           show version\n");
}

static void run_with_signals(const char *line)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    csx_run_line(line);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
}

/* Source ~/.catshellxrc at startup (interactive shells only). */
static void load_rc(void)
{
    const char *home = getenv("HOME");
    char path[4096];
    FILE *f;
    char *line = NULL;
    size_t cap = 0;

    if (!home)
        return;
    snprintf(path, sizeof(path), "%s/.catshellxrc", home);
    f = fopen(path, "r");
    if (!f)
        return;
    while (csx_getline(&line, &cap, f) >= 0) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        char *e = p + strlen(p);
        while (e > p && (e[-1] == '\n' || e[-1] == '\r'))
            *--e = '\0';
        if (*p && *p != '#')
            csx_run_line(p);
        if (csx_exit_requested)
            break;
    }
    free(line);
    fclose(f);
}

/* Make `cat_config` usable even when /usr/local/bin is not in PATH (a
 * terminal may launch the shell without sourcing ~/.bash_profile). The
 * user's own alias wins; if the command is already reachable via PATH, or
 * is not installed at the default location, nothing is added. */
static void install_default_aliases(void)
{
    if (csx_alias_get("cat_config"))
        return;
    if (access("/usr/local/bin/cat_config", X_OK) != 0)
        return;
    const char *path = getenv("PATH");
    if (path) {
        char *copy = strdup(path);
        if (copy) {
            char *save = NULL;
            char *d = strtok_r(copy, ":", &save);
            while (d) {
                char full[4096];
                snprintf(full, sizeof(full), "%s/cat_config", d);
                if (access(full, X_OK) == 0) {
                    free(copy);
                    return;
                }
                d = strtok_r(NULL, ":", &save);
            }
            free(copy);
        }
    }
    csx_alias_set("cat_config", "/usr/local/bin/cat_config");
}

static int interactive_loop(void)
{
    csx_job_init();
    load_rc();
    install_default_aliases();
    if (csx_raw_mode(1) != 0)
        return 1;
    hist_load();

    for (;;) {
        strbuf p;
        sb_init(&p);
        size_t pw = csx_prompt(&p);
        char *line = csx_readline(sb_str(&p), pw, csx_suggest);
        sb_free(&p);

        if (!line) {
            if (csx_eof || !csx_cancelled) {
                printf("\n");
                break;
            }
            continue;
        }

        if (line[0] == '\0') {
            free(line);
            continue;
        }

        hist_add(line);
        csx_raw_mode(0);
        run_with_signals(line);
        csx_raw_mode(1);
        free(line);

        if (csx_exit_requested)
            break;
    }

    csx_raw_mode(0);
    hist_save();
    csx_job_shutdown();
    csx_var_shutdown();
    csx_alias_shutdown();
    hist_shutdown();
    return 0;
}

int main(int argc, char **argv)
{
    csx_argv0 = argv;
    const char *cmd = NULL;

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            cmd = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("%s %s\n", CSX_NAME, CSX_VERSION);
            return 0;
        } else {
            fprintf(stderr, "catshellx: unknown option: %s\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }

    csx_var_init();
    if (cmd) {
        int r = csx_run_line(cmd);
        csx_job_shutdown();
        csx_var_shutdown();
        csx_alias_shutdown();
        hist_shutdown();
        return r;
    }

    int interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

    if (!interactive) {
        char *line = NULL;
        size_t cap = 0;
        while (csx_getline(&line, &cap, stdin) >= 0) {
            char *p = line;
            while (*p == ' ' || *p == '\t')
                p++;
            char *e = p + strlen(p);
            while (e > p && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = '\0';
            if (*p)
                csx_run_line(p);
            if (csx_exit_requested)
                break;
        }
        free(line);
        csx_job_shutdown();
        csx_var_shutdown();
        csx_alias_shutdown();
        hist_shutdown();
        return 0;
    }

    return interactive_loop();
}
