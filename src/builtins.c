#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

int csx_is_builtin(const char *name)
{
    static const char *builtins[] = {
        "cd", "pwd", "echo", "exit", "true", "false", "help", "type", "env",
        "set", "export", "unset", "alias", "unalias", "source", ".",
        "jobs", "fg", "bg", NULL
    };
    int i;
    for (i = 0; builtins[i]; i++)
        if (strcmp(builtins[i], name) == 0)
            return 1;
    return 0;
}

const char *const *csx_builtin_list(void)
{
    static const char *builtins[] = {
        "cd", "pwd", "echo", "exit", "true", "false", "help", "type", "env",
        "set", "export", "unset", "alias", "unalias", "source", ".",
        "jobs", "fg", "bg", NULL
    };
    return builtins;
}

static char *search_path(const char *name)
{
    if (strchr(name, '/'))
        return access(name, X_OK) == 0 ? strdup(name) : NULL;
    const char *path = getenv("PATH");
    if (!path)
        return NULL;
    char *copy = strdup(path);
    char *save = NULL;
    char *d = strtok_r(copy, ":", &save);
    while (d) {
        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", d, name);
        if (access(full, X_OK) == 0) {
            char *r = strdup(full);
            free(copy);
            return r;
        }
        d = strtok_r(NULL, ":", &save);
    }
    free(copy);
    return NULL;
}

int csx_command_exists(const char *name)
{
    if (csx_is_builtin(name))
        return 1;
    if (csx_alias_get(name))
        return 1;
    if (strchr(name, '/'))
        return access(name, X_OK) == 0;
    {
        char *full = search_path(name);
        if (full) {
            free(full);
            return 1;
        }
    }
    return 0;
}

static int builtin_env(char **args)
{
    extern char **environ;
    int i;
    (void)args;
    for (i = 0; environ[i]; i++)
        printf("%s\n", environ[i]);
    return 0;
}

int csx_run_builtin(char **args)
{
    const char *cmd = args[0];

    if (strcmp(cmd, "cd") == 0) {
        const char *dir = args[1] ? args[1] : getenv("HOME");
        if (!dir) {
            fprintf(stderr, "cd: HOME not set\n");
            return 1;
        }
        if (chdir(dir) != 0) {
            fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
            return 1;
        }
        csx_var_sync_cwd();
        return 0;
    }
    if (strcmp(cmd, "pwd") == 0) {
        char cwd[4096];
        if (!getcwd(cwd, sizeof(cwd))) {
            perror("pwd");
            return 1;
        }
        printf("%s\n", cwd);
        return 0;
    }
    if (strcmp(cmd, "echo") == 0) {
        int nflag = 0, start = 1, i;
        while (args[start] && strcmp(args[start], "-n") == 0) {
            nflag = 1;
            start++;
        }
        for (i = start; args[i]; i++) {
            if (i > start)
                putchar(' ');
            fputs(args[i], stdout);
        }
        if (!nflag)
            putchar('\n');
        return 0;
    }
    if (strcmp(cmd, "exit") == 0) {
        int code = args[1] ? atoi(args[1]) : 0;
        csx_exit_requested = 1;
        return code;
    }
    if (strcmp(cmd, "true") == 0)
        return 0;
    if (strcmp(cmd, "false") == 0)
        return 1;
    if (strcmp(cmd, "env") == 0)
        return builtin_env(args);
    if (strcmp(cmd, "set") == 0) {
        int i = 1;
        if (!args[1]) {
            csx_var_list(stdout, 0);
            return 0;
        }
        for (; args[i]; i++) {
            const char *eq = strchr(args[i], '=');
            if (eq) {
                strbuf nm;
                sb_init(&nm);
                sb_nputs(&nm, args[i], (size_t)(eq - args[i]));
                csx_var_set(sb_str(&nm), eq + 1, 0);
                sb_free(&nm);
            } else if (args[i + 1]) {
                csx_var_set(args[i], args[i + 1], 0);
                i++;
            } else {
                fprintf(stderr, "set: invalid operand: %s\n", args[i]);
                return 1;
            }
        }
        return 0;
    }
    if (strcmp(cmd, "export") == 0) {
        int i = 1;
        if (!args[1]) {
            csx_var_list(stdout, 1);
            return 0;
        }
        for (; args[i]; i++) {
            const char *eq = strchr(args[i], '=');
            if (eq) {
                strbuf nm;
                sb_init(&nm);
                sb_nputs(&nm, args[i], (size_t)(eq - args[i]));
                csx_var_set(sb_str(&nm), eq + 1, 1);
                sb_free(&nm);
            } else {
                const char *v = csx_var_get(args[i]);
                csx_var_set(args[i], v ? v : "", 1);
            }
        }
        return 0;
    }
    if (strcmp(cmd, "unset") == 0) {
        int i;
        if (!args[1]) {
            fprintf(stderr, "unset: missing operand\n");
            return 1;
        }
        for (i = 1; args[i]; i++)
            csx_var_unset(args[i]);
        return 0;
    }
    if (strcmp(cmd, "alias") == 0) {
        int i = 1;
        if (!args[1]) {
            csx_alias_list(stdout);
            return 0;
        }
        for (; args[i]; i++) {
            const char *eq = strchr(args[i], '=');
            if (eq) {
                strbuf nm;
                sb_init(&nm);
                sb_nputs(&nm, args[i], (size_t)(eq - args[i]));
                csx_alias_set(sb_str(&nm), eq + 1);
                sb_free(&nm);
            } else {
                const char *v = csx_alias_get(args[i]);
                if (v)
                    printf("alias %s='%s'\n", args[i], v);
                else {
                    fprintf(stderr, "alias: %s: not found\n", args[i]);
                    return 1;
                }
            }
        }
        return 0;
    }
    if (strcmp(cmd, "unalias") == 0) {
        int i;
        if (!args[1]) {
            fprintf(stderr, "unalias: missing operand\n");
            return 1;
        }
        for (i = 1; args[i]; i++)
            csx_alias_unset(args[i]);
        return 0;
    }
    if (strcmp(cmd, "source") == 0 || strcmp(cmd, ".") == 0) {
        FILE *f;
        char *line = NULL;
        size_t cap = 0;
        int last = 0;
        if (!args[1]) {
            fprintf(stderr, "source: missing file argument\n");
            return 1;
        }
        f = fopen(args[1], "r");
        if (!f) {
            fprintf(stderr, "source: %s: %s\n", args[1], strerror(errno));
            return 1;
        }
        while (csx_getline(&line, &cap, f) >= 0) {
            char *p = line;
            while (*p == ' ' || *p == '\t')
                p++;
            char *e = p + strlen(p);
            while (e > p && (e[-1] == '\n' || e[-1] == '\r'))
                *--e = '\0';
            if (*p && *p != '#')
                last = csx_run_line(p);
            if (csx_exit_requested)
                break;
        }
        free(line);
        fclose(f);
        return last;
    }
    if (strcmp(cmd, "jobs") == 0)
        return csx_jobs();
    if (strcmp(cmd, "fg") == 0)
        return csx_job_fg(args[1]);
    if (strcmp(cmd, "bg") == 0)
        return csx_job_bg(args[1]);
    if (strcmp(cmd, "type") == 0) {
        if (!args[1]) {
            fprintf(stderr, "type: missing operand\n");
            return 1;
        }
        if (csx_is_builtin(args[1])) {
            printf("%s is a shell builtin\n", args[1]);
            return 0;
        }
        {
            const char *av = csx_alias_get(args[1]);
            if (av) {
                printf("%s is an alias for %s\n", args[1], av);
                return 0;
            }
        }
        char *full = search_path(args[1]);
        if (full) {
            printf("%s is %s\n", args[1], full);
            free(full);
            return 0;
        }
        printf("%s not found\n", args[1]);
        return 1;
    }
    if (strcmp(cmd, "help") == 0) {
        printf("CatShellX builtins:\n");
        printf("  cd [dir]   pwd   echo [-n] [args...]   exit [code]\n");
        printf("  true   false   type <name>   env   help\n");
        printf("  set [name[=value]...]   export [name[=value]...]   unset <name>...\n");
        printf("  alias [name[=value]...]   unalias <name>...   source <file>\n");
        printf("  jobs [%%job...]   fg [%%job]   bg [%%job]   (%%N or %%prefix)\n");
        return 0;
    }
    fprintf(stderr, "catshellx: unknown builtin: %s\n", cmd);
    return 127;
}
