#ifndef CSX_SHELL_H
#define CSX_SHELL_H

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>

#define CSX_NAME    "CatShellX"
#define CSX_VERSION "0.3.1"

/* ------------------------------------------------------------------ */
/* String builder (replaces asprintf/getline, absent on Tiger 10.4)   */
/* ------------------------------------------------------------------ */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} strbuf;

void  sb_init(strbuf *sb);
void  sb_free(strbuf *sb);
void  sb_reset(strbuf *sb);
int   sb_ensure(strbuf *sb, size_t extra);
int   sb_putc(strbuf *sb, char c);
int   sb_puts(strbuf *sb, const char *s);
int   sb_nputs(strbuf *sb, const char *s, size_t n);
int   sb_printf(strbuf *sb, const char *fmt, ...) __attribute__((format(printf,2,3)));
char *sb_str(const strbuf *sb);
char *sb_detach(strbuf *sb);

/* Portable getline replacement (not present in Tiger libc). */
ssize_t csx_getline(char **lineptr, size_t *n, FILE *stream);

/* Case-insensitive substring search (strcasestr absent on Tiger). */
char *csx_strcasestr(const char *haystack, const char *needle);

/* ------------------------------------------------------------------ */
/* Shell state                                                         */
/* ------------------------------------------------------------------ */
extern int csx_exit_requested;
extern char **csx_argv0;
extern int csx_last_status;

/* ------------------------------------------------------------------ */
/* Execution                                                           */
/* ------------------------------------------------------------------ */
int csx_run_line(const char *line);
int csx_is_builtin(const char *name);
int csx_run_builtin(char **args);
const char *const *csx_builtin_list(void);

/* ------------------------------------------------------------------ */
/* Job control (jobs/fg/bg, SIGINT/SIGTSTP)                            */
/* ------------------------------------------------------------------ */
void csx_job_init(void);
int  csx_jobs(void);
int  csx_job_fg(const char *spec);
int  csx_job_bg(const char *spec);
void csx_job_shutdown(void);

/* ------------------------------------------------------------------ */
/* Shell variables (set/export/unset, $PWD/$OLDPWD sync)               */
/* ------------------------------------------------------------------ */
int  csx_var_name_ok(const char *name);
const char *csx_var_get(const char *name);
int  csx_bool_var(const char *name, int def);
void csx_var_set(const char *name, const char *value, int exported);
void csx_var_unset(const char *name);
void csx_var_list(FILE *out, int only_exported);
void csx_var_init(void);
void csx_var_sync_cwd(void);
void csx_var_shutdown(void);

/* ------------------------------------------------------------------ */
/* Aliases (alias/unalias, first-token expansion)                      */
/* ------------------------------------------------------------------ */
void csx_alias_set(const char *name, const char *value);
const char *csx_alias_get(const char *name);
void csx_alias_unset(const char *name);
int  csx_alias_list(FILE *out);
size_t csx_alias_count(void);
const char *csx_alias_name(size_t i);
int  csx_alias_expand_cmd(char **words, int *nwords);
void csx_alias_shutdown(void);

/* ------------------------------------------------------------------ */
/* History                                                             */
/* ------------------------------------------------------------------ */
void hist_load(void);
void hist_save(void);
void hist_add(const char *line);
void hist_nav_reset(void);
const char *hist_nav_prev(void);
const char *hist_nav_next(void);
const char *hist_search(const char *q);
const char *hist_search_older(const char *q);
const char *hist_find_prefix(const char *p);
void hist_shutdown(void);

/* ------------------------------------------------------------------ */
/* Line editor                                                         */
/* ------------------------------------------------------------------ */
int csx_raw_mode(int on);
char *csx_readline(const char *prompt, size_t prompt_width,
                   const char *(*suggest)(const char *));
extern int csx_eof;
extern int csx_cancelled;

/* ------------------------------------------------------------------ */
/* Autosuggestion                                                      */
/* ------------------------------------------------------------------ */
const char *csx_suggest(const char *line);

/* ------------------------------------------------------------------ */
/* Tab completion                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    char **items;
    size_t count;
    size_t base;
} csx_completion;

int csx_complete_line(const char *line, size_t pos, csx_completion *out);
void csx_completion_free(csx_completion *c);

/* ------------------------------------------------------------------ */
/* Prompt                                                              */
/* ------------------------------------------------------------------ */
size_t csx_prompt(strbuf *out);
void csx_build_title(strbuf *out);
void csx_render_tpl(strbuf *out, const char *tpl);
void csx_build_greeting(strbuf *out);

/* ------------------------------------------------------------------ */
/* Terminal window title (OSC 0, TerminalX/xterm)                      */
/* ------------------------------------------------------------------ */
int  csx_title_enabled(void);
void csx_title_set(const char *t);
void csx_title_clear(void);

/* ------------------------------------------------------------------ */
/* Word expansion (tildes, $VAR, ${}, $(), $?, braces, globs)          */
/* ------------------------------------------------------------------ */
typedef struct {
    char **items;
    size_t count;
    size_t cap;
} csx_wordlist;

void csx_wl_init(csx_wordlist *wl);
void csx_wl_free(csx_wordlist *wl);
int  csx_expand_word(const char *raw, csx_wordlist *wl);

/* ------------------------------------------------------------------ */
/* Syntax highlighting                                                  */
/* ------------------------------------------------------------------ */
int  csx_command_exists(const char *name);
void csx_highlight(const char *line, strbuf *out);

/* ------------------------------------------------------------------ */
/* ANSI colors (used by prompt/highlight/editor)                       */
/* ------------------------------------------------------------------ */
#define CSX_C_RESET   "\x1b[0m"
#define CSX_C_BOLD    "\x1b[1m"
#define CSX_C_DIM     "\x1b[2m"
#define CSX_C_RED     "\x1b[31m"
#define CSX_C_GREEN   "\x1b[32m"
#define CSX_C_YELLOW  "\x1b[33m"
#define CSX_C_BLUE    "\x1b[34m"
#define CSX_C_MAGENTA "\x1b[35m"
#define CSX_C_CYAN    "\x1b[36m"
#define CSX_C_WHITE   "\x1b[37m"
#define CSX_C_BGRAY   "\x1b[90m"
#define CSX_C_BRED    "\x1b[1;31m"
#define CSX_C_BGREEN  "\x1b[1;32m"
#define CSX_C_BYELLOW "\x1b[1;33m"
#define CSX_C_BBLUE   "\x1b[1;34m"
#define CSX_C_BMAGENTA "\x1b[1;35m"
#define CSX_C_BCYAN   "\x1b[1;36m"
#define CSX_C_BWHITE  "\x1b[1;37m"

#endif /* CSX_SHELL_H */
