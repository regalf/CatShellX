#include "shell.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>

int csx_exit_requested = 0;
char **csx_argv0 = NULL;

/* ------------------------------------------------------------------ */
/* Job control                                                         */
/* ------------------------------------------------------------------ */

#define JOB_MAX 64

typedef struct {
    pid_t pgid;
    int n;
    pid_t pids[CSX_MAX_CMDS];
    char *cmd;
    int id;
    int running;
    int stopped;
    int status;
} csx_job;

static csx_job jobs[JOB_MAX];
static int job_seq = 0;
static csx_job *last_job = NULL;

static int shell_terminal = -1;
static pid_t shell_pgid = 0;
static int job_control = 0;

void csx_job_init(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        job_control = 0;
        return;
    }
    shell_terminal = STDIN_FILENO;
    shell_pgid = getpgrp();
    if (shell_pgid != getpid()) {
        setpgid(0, 0);
        shell_pgid = getpid();
    }
    signal(SIGTTOU, SIG_IGN);
    if (tcsetpgrp(shell_terminal, shell_pgid) < 0) {
        job_control = 0;
        return;
    }
    job_control = 1;
}

static csx_job *job_slot(void)
{
    int i;
    for (i = 0; i < JOB_MAX; i++)
        if (!jobs[i].cmd)
            return &jobs[i];
    return NULL;
}

static void job_free(csx_job *j)
{
    if (last_job == j)
        last_job = NULL;
    free(j->cmd);
    memset(j, 0, sizeof *j);
}

static csx_job *job_add(pid_t pgid, int n, const pid_t *pids, const char *cmdline)
{
    csx_job *j = job_slot();
    int k;
    if (!j)
        return NULL;
    memset(j, 0, sizeof *j);
    j->id = ++job_seq;
    j->pgid = pgid;
    j->n = n;
    for (k = 0; k < n && k < CSX_MAX_CMDS; k++)
        j->pids[k] = pids[k];
    j->cmd = strdup(cmdline ? cmdline : "");
    j->running = 1;
    last_job = j;
    return j;
}

static csx_job *job_find_id(int id)
{
    int i;
    for (i = 0; i < JOB_MAX; i++)
        if (jobs[i].cmd && jobs[i].id == id)
            return &jobs[i];
    return NULL;
}

static csx_job *job_find_prefix(const char *s)
{
    csx_job *found = NULL;
    int best = 0;
    int i;
    size_t sl = strlen(s);
    for (i = 0; i < JOB_MAX; i++) {
        csx_job *j = &jobs[i];
        if (j->cmd && j->id > best && strncmp(j->cmd, s, sl) == 0) {
            best = j->id;
            found = j;
        }
    }
    return found;
}

static csx_job *job_resolve(const char *spec)
{
    const char *s = spec;
    if (!s || !*s || strcmp(s, "%") == 0)
        return (last_job && last_job->cmd) ? last_job : NULL;
    if (*s == '%')
        s++;
    if (isdigit((unsigned char)*s))
        return job_find_id(atoi(s));
    return job_find_prefix(s);
}

/* WNOHANG-reap finished processes. Returns 1 when the job fully finished. */
static int job_update(csx_job *j)
{
    int alive = 0;
    int status = 0;
    int k;
    for (k = 0; k < j->n; k++) {
        int st;
        pid_t r;
        if (j->pids[k] <= 0)
            continue;
        do {
            r = waitpid(j->pids[k], &st, WNOHANG);
        } while (r < 0 && errno == EINTR);
        if (r == 0) {
            alive = 1;
            continue;
        }
        if (r < 0) {
            j->pids[k] = -1;
            continue;
        }
        j->pids[k] = -1;
        if (WIFEXITED(st))
            status = WEXITSTATUS(st);
        else if (WIFSIGNALED(st))
            status = 128 + WTERMSIG(st);
        else
            status = 1;
    }
    if (!alive) {
        j->running = 0;
        j->stopped = 0;
        j->status = status;
        return 1;
    }
    return 0;
}

/* Report finished background jobs, just before the next prompt. */
static void job_reap_all(void)
{
    int i;
    for (i = 0; i < JOB_MAX; i++) {
        csx_job *j = &jobs[i];
        if (!j->cmd)
            continue;
        if (job_update(j)) {
            printf("[%d]  Done    %s\n", j->id, j->cmd);
            fflush(stdout);
            job_free(j);
        }
    }
}

/* Block until the job finishes or is stopped by a terminal signal. */
static int wait_foreground(csx_job *j)
{
    int last = 1;
    int k;
    int stopped = 0;

    for (k = 0; k < j->n; k++) {
        int st;
        pid_t r;
        if (j->pids[k] <= 0)
            continue;
        do {
            r = waitpid(j->pids[k], &st, WUNTRACED);
        } while (r < 0 && errno == EINTR);
        if (r < 0) {
            j->pids[k] = -1;
            continue;
        }
        if (WIFSTOPPED(st)) {
            stopped = 1;
            break;
        }
        j->pids[k] = -1;
        if (k == j->n - 1) {
            if (WIFEXITED(st))
                last = WEXITSTATUS(st);
            else if (WIFSIGNALED(st))
                last = 128 + WTERMSIG(st);
            else
                last = 1;
        }
    }

    if (stopped) {
        j->running = 0;
        j->stopped = 1;
        printf("[%d]  Stopped %s\n", j->id, j->cmd);
        fflush(stdout);
        return 0;
    }
    job_free(j);
    return last;
}

int csx_jobs(void)
{
    int i, count = 0;
    for (i = 0; i < JOB_MAX; i++) {
        csx_job *j = &jobs[i];
        if (!j->cmd)
            continue;
        count++;
        if (job_update(j)) {
            printf("[%d]  Done    %s\n", j->id, j->cmd);
            job_free(j);
        } else if (j->stopped) {
            printf("[%d]  Stopped %s\n", j->id, j->cmd);
        } else {
            printf("[%d]  Running %s\n", j->id, j->cmd);
        }
    }
    (void)count;
    return 0;
}

int csx_job_fg(const char *spec)
{
    csx_job *j = job_resolve(spec);
    if (!j) {
        fprintf(stderr, "fg: no such job\n");
        return 1;
    }
    if (!j->running && !j->stopped) {
        int st = j->status;
        printf("[%d]  Done    %s\n", j->id, j->cmd);
        job_free(j);
        return st;
    }
    if (job_control) {
        if (tcsetpgrp(shell_terminal, j->pgid) < 0)
            perror("fg");
        if (j->stopped)
            kill(-j->pgid, SIGCONT);
        j->stopped = 0;
        {
            struct termios t;
            if (tcgetattr(shell_terminal, &t) == 0) {
                t.c_lflag |= ISIG;
                tcsetattr(shell_terminal, TCSANOW, &t);
            }
        }
    }
    j->running = 1;
    printf("%s\n", j->cmd);
    fflush(stdout);
    int fr = wait_foreground(j);
    if (job_control)
        tcsetpgrp(shell_terminal, shell_pgid);
    return fr;
}

int csx_job_bg(const char *spec)
{
    csx_job *j = job_resolve(spec);
    if (!j) {
        fprintf(stderr, "bg: no such job\n");
        return 1;
    }
    if (!j->stopped) {
        fprintf(stderr, "bg: job %d already in background\n", j->id);
        return 1;
    }
    j->stopped = 0;
    j->running = 1;
    if (job_control && kill(-j->pgid, SIGCONT) < 0)
        perror("bg");
    printf("[%d] %s\n", j->id, j->cmd);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Redirections                                                        */
/* ------------------------------------------------------------------ */

static int apply_redirs(csx_cmd *c)
{
    int i;
    for (i = 0; i < c->nredirs; i++) {
        csx_redir *r = &c->redirs[i];
        if (r->is_dup) {
            if (dup2(r->dup_fd, r->fd) < 0) {
                fprintf(stderr, "catshellx: dup2: %s\n", strerror(errno));
                return -1;
            }
        } else if (r->target) {
            int flags;
            if (r->mode == CSX_REDIR_IN)
                flags = O_RDONLY;
            else if (r->mode == CSX_REDIR_APPEND)
                flags = O_WRONLY | O_CREAT | O_APPEND;
            else
                flags = O_WRONLY | O_CREAT | O_TRUNC;
            int fd = open(r->target, flags, 0666);
            if (fd < 0) {
                fprintf(stderr, "catshellx: %s: %s\n", r->target, strerror(errno));
                return -1;
            }
            if (fd != r->fd) {
                if (dup2(fd, r->fd) < 0) {
                    fprintf(stderr, "catshellx: dup2: %s\n", strerror(errno));
                    close(fd);
                    return -1;
                }
                close(fd);
            }
        } else {
            fprintf(stderr, "catshellx: redirection target missing\n");
            return -1;
        }
    }
    return 0;
}

static int run_builtin_redirected(csx_cmd *c, char **argv)
{
    int saved[3], bak[3], nsaved = 0;
    int i, j;

    for (i = 0; i < c->nredirs; i++) {
        int fd = c->redirs[i].fd;
        int already = 0;
        if (fd < 0 || fd > 2)
            continue;
        for (j = 0; j < nsaved; j++)
            if (saved[j] == fd)
                already = 1;
        if (!already) {
            int nf = fcntl(fd, F_DUPFD, 10);
            if (nf < 0) {
                fprintf(stderr, "catshellx: fcntl: %s\n", strerror(errno));
                for (j = 0; j < nsaved; j++)
                    close(bak[j]);
                return 1;
            }
            saved[nsaved] = fd;
            bak[nsaved] = nf;
            nsaved++;
        }
    }
    if (apply_redirs(c) != 0) {
        for (j = 0; j < nsaved; j++) {
            dup2(bak[j], saved[j]);
            close(bak[j]);
        }
        return 1;
    }
    int r = csx_run_builtin(argv);
    for (j = 0; j < nsaved; j++) {
        dup2(bak[j], saved[j]);
        close(bak[j]);
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* Deferred word expansion                                             */
/* ------------------------------------------------------------------ */

/* Expand the raw tokens of c into a malloc'd NULL-terminated argv
 * array.  Runs in the parent at execution time so $?, $() and $VAR see
 * the state left by the previous command.  *out_n gets the word count;
 * returns NULL on OOM.  Caller frees with argv_free().
 */
static char **expand_cmd_words(csx_cmd *c, int *out_n)
{
    char **argv;
    int n = 0;
    int i;

    argv = calloc(CSX_MAX_WORDS + 1, sizeof(char *));
    if (!argv) {
        *out_n = 0;
        return NULL;
    }
    for (i = 0; i < c->nwords; i++) {
        csx_wordlist wl;
        size_t k;
        if (csx_expand_word(c->words[i], &wl) == 0)
            continue;
        for (k = 0; k < wl.count; k++) {
            if (!wl.items[k])
                continue;
            if (n < CSX_MAX_WORDS)
                argv[n++] = wl.items[k];
            else
                free(wl.items[k]);
        }
        free(wl.items);
    }
    argv[n] = NULL;
    *out_n = n;
    return argv;
}

static void argv_free(char **argv, int n)
{
    int i;
    if (!argv)
        return;
    for (i = 0; i < n; i++)
        free(argv[i]);
    free(argv);
}

/* Expand a redirection target (raw token) into a concrete filename. */
static void expand_redir_targets(csx_cmd *c)
{
    int i;
    for (i = 0; i < c->nredirs; i++) {
        csx_redir *r = &c->redirs[i];
        csx_wordlist wl;
        size_t k;
        if (r->is_dup || !r->target)
            continue;
        if (csx_expand_word(r->target, &wl) == 0) {
            free(r->target);
            r->target = NULL;
            continue;
        }
        free(r->target);
        r->target = wl.items[0];
        for (k = 1; k < wl.count; k++)
            free(wl.items[k]);
        free(wl.items);
    }
}

/* ------------------------------------------------------------------ */
/* Pipeline execution                                                  */
/* ------------------------------------------------------------------ */

static void child_signal_reset(void)
{
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
}

/* Foreground children need ISIG so Ctrl-C/Ctrl-Z generate real signals. */
static void child_enable_isig(void)
{
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) == 0) {
        t.c_lflag |= ISIG;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
}

static int run_pipeline(csx_node *node, const char *line)
{
    int n = node->ncmds;
    if (n <= 0)
        return 0;

    char **argv[CSX_MAX_CMDS];
    int nargv[CSX_MAX_CMDS];
    int ci;
    int has_command = 0;

    for (ci = 0; ci < n; ci++) {
        csx_cmd *c = &node->cmds[ci];
        argv[ci] = NULL;
        nargv[ci] = 0;
        expand_redir_targets(c);
        if (c->nwords > 0)
            argv[ci] = expand_cmd_words(c, &nargv[ci]);
        if (argv[ci] && nargv[ci] > 0)
            csx_alias_expand_cmd(argv[ci], &nargv[ci]);
        if (nargv[ci] > 0 || c->nredirs > 0)
            has_command = 1;
    }

    if (!has_command) {
        for (ci = 0; ci < n; ci++)
            argv_free(argv[ci], nargv[ci]);
        return csx_last_status;
    }

    /* Fast path: a single builtin with redirections runs in-process. */
    if (n == 1 && nargv[0] > 0) {
        csx_cmd *c = &node->cmds[0];
        if (csx_is_builtin(argv[0][0])) {
            if (node->background) {
                fflush(NULL);
                pid_t pid = fork();
                if (pid == 0) {
                    setpgid(0, 0);
                    child_signal_reset();
                    if (apply_redirs(c) != 0)
                        _exit(1);
                    int r = csx_run_builtin(argv[0]);
                    fflush(NULL);
                    _exit(r);
                }
                if (pid > 0) {
                    pid_t p = pid;
                    if (job_control)
                        setpgid(pid, pid);
                    if (!job_add(pid, 1, &p, line)) {
                        fprintf(stderr, "catshellx: job table full\n");
                        waitpid(pid, NULL, WNOHANG);
                    }
                    argv_free(argv[0], nargv[0]);
                    return 0;
                }
                argv_free(argv[0], nargv[0]);
                fprintf(stderr, "catshellx: fork: %s\n", strerror(errno));
                return 1;
            }
            int r = run_builtin_redirected(c, argv[0]);
            argv_free(argv[0], nargv[0]);
            return r;
        }
    }

    pid_t pids[CSX_MAX_CMDS];
    int np = 0;
    pid_t pgid = 0;
    int prevpipe = -1;
    int i;
    int pipe_ok = 1;

    for (i = 0; i < n; i++) {
        csx_cmd *c = &node->cmds[i];
        int curpipe[2] = { -1, -1 };
        if (i < n - 1 && pipe(curpipe) != 0) {
            fprintf(stderr, "catshellx: pipe: %s\n", strerror(errno));
            pipe_ok = 0;
            break;
        }
        fflush(NULL);
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "catshellx: fork: %s\n", strerror(errno));
            pipe_ok = 0;
            if (curpipe[0] >= 0)
                close(curpipe[0]);
            if (curpipe[1] >= 0)
                close(curpipe[1]);
            break;
        }
        if (pid == 0) {
            if (job_control)
                setpgid(0, pgid ? pgid : 0);
            child_signal_reset();
            if (!node->background && job_control)
                child_enable_isig();
            if (prevpipe >= 0) {
                dup2(prevpipe, 0);
                close(prevpipe);
            }
            if (curpipe[1] >= 0)
                dup2(curpipe[1], 1);
            if (curpipe[0] >= 0)
                close(curpipe[0]);
            if (curpipe[1] >= 0)
                close(curpipe[1]);
            if (apply_redirs(c) != 0)
                _exit(1);
            if (nargv[i] == 0)
                _exit(0);
            if (csx_is_builtin(argv[i][0])) {
                int r = csx_run_builtin(argv[i]);
                fflush(NULL);
                _exit(r);
            }
            execvp(argv[i][0], argv[i]);
            fprintf(stderr, "catshellx: %s: %s\n", argv[i][0], strerror(errno));
            _exit(127);
        }
        if (pgid == 0)
            pgid = pid;
        if (job_control)
            setpgid(pid, pgid);
        if (prevpipe >= 0)
            close(prevpipe);
        if (curpipe[1] >= 0)
            close(curpipe[1]);
        prevpipe = curpipe[0];
        pids[np++] = pid;
    }
    if (prevpipe >= 0)
        close(prevpipe);

    int status = 1;
    if (node->background) {
        if (np > 0 && !job_add(pgid, np, pids, line)) {
            fprintf(stderr, "catshellx: job table full\n");
            for (i = 0; i < np; i++)
                waitpid(pids[i], NULL, WNOHANG);
        }
    } else if (np > 0) {
        csx_job *j = job_add(pgid, np, pids, line);
        if (job_control && tcsetpgrp(shell_terminal, pgid) < 0)
            perror("catshellx");
        if (j) {
            status = wait_foreground(j);
        } else {
            fprintf(stderr, "catshellx: job table full\n");
            for (i = 0; i < np; i++) {
                int st;
                pid_t r;
                do {
                    r = waitpid(pids[i], &st, 0);
                } while (r < 0 && errno == EINTR);
                if (r >= 0 && i == np - 1) {
                    if (WIFEXITED(st))
                        status = WEXITSTATUS(st);
                    else if (WIFSIGNALED(st))
                        status = 128 + WTERMSIG(st);
                }
            }
        }
        if (job_control)
            tcsetpgrp(shell_terminal, shell_pgid);
    }

    for (ci = 0; ci < n; ci++)
        argv_free(argv[ci], nargv[ci]);
    return pipe_ok ? status : 1;
}

int csx_run_line(const char *line)
{
    const char *s = line;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '#')
        return 0;

    csx_node *n = csx_parse(line);
    if (!n)
        return 0;

    job_reap_all();
    int skip_next = 0;
    int last = 1;
    csx_node *cur;
    for (cur = n; cur; cur = cur->next) {
        if (!skip_next)
            last = run_pipeline(cur, line);
        else
            last = 1;
        csx_last_status = last;
        skip_next = 0;
        if (cur->op == CSX_OP_AND && last != 0)
            skip_next = 1;
        else if (cur->op == CSX_OP_OR && last == 0)
            skip_next = 1;
    }
    csx_free_node(n);
    csx_last_status = last;
    return last;
}

void csx_job_shutdown(void)
{
    int i;
    for (i = 0; i < JOB_MAX; i++)
        job_free(&jobs[i]);
}
