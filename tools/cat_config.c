/*
 * cat_config - interactive TUI configuration tool for CatShellX.
 *
 * A small, dependency-free (no ncurses) menu-driven editor for
 * ~/.catshellxrc: prompt/title templates with live preview, aliases,
 * behavior toggles, and a save step that keeps a backup.
 *
 * Only lines it manages are rewritten; any other rc content (export,
 * source, comments, ...) is preserved verbatim.
 */

#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* ANSI styling                                                       */
/* ------------------------------------------------------------------ */
#define C_RESET  "\x1b[0m"
#define C_BOLD   "\x1b[1m"
#define C_DIM    "\x1b[2m"
#define C_RED    "\x1b[31m"
#define C_GREEN  "\x1b[32m"
#define C_YELLOW "\x1b[1;33m"
#define C_CYAN   "\x1b[1;36m"

/* ------------------------------------------------------------------ */
/* key codes (negative => escape sequences)                           */
/* ------------------------------------------------------------------ */
#define K_UP      -1
#define K_DOWN    -2
#define K_RIGHT   -3
#define K_LEFT    -4
#define K_HOME    -5
#define K_END     -6
#define K_DEL     -7
#define K_READERR -8
#define K_ESC     0x1b
#define K_ENTER   '\r'
#define K_BACKSPACE 0x7f

static struct termios cfg_orig_tio;
static int cfg_raw_on = 0;

static int raw_mode(int on)
{
    if (on && !cfg_raw_on) {
        if (tcgetattr(0, &cfg_orig_tio) != 0)
            return -1;
        struct termios t = cfg_orig_tio;
        t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
        t.c_iflag &= ~(IXON | ICRNL | INLCR);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        if (tcsetattr(0, TCSANOW, &t) != 0)
            return -1;
        cfg_raw_on = 1;
    } else if (!on && cfg_raw_on) {
        tcsetattr(0, TCSANOW, &cfg_orig_tio);
        cfg_raw_on = 0;
    }
    return 0;
}

static int read_byte(void)
{
    unsigned char c;
    ssize_t n;
    while ((n = read(0, &c, 1)) < 0 && errno == EINTR)
        ;
    if (n <= 0)
        return K_READERR;
    return c;
}

static int read_key(void)
{
    int c = read_byte();
    if (c != 0x1b)
        return c;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 30000;
    if (select(1, &fds, NULL, NULL, &tv) <= 0)
        return K_ESC;

    int c2 = read_byte();
    if (c2 == '[' || c2 == 'O') {
        int c3 = read_byte();
        switch (c3) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        case 'H': return K_HOME;
        case 'F': return K_END;
        case '1':
        case '7': { int c4 = read_byte(); if (c4 == '~') return K_HOME; return c3; }
        case '3': { int c4 = read_byte(); if (c4 == '~') return K_DEL; return c3; }
        case '4':
        case '8': { int c4 = read_byte(); if (c4 == '~') return K_END; return c3; }
        default: return c3;
        }
    }
    return K_ESC;
}

static void cls(void)
{
    fputs("\x1b[2J\x1b[H", stdout);
    fflush(stdout);
}

static void gotoxy(int row, int col)
{
    printf("\x1b[%d;%dH", row, col);
}

static int term_rows(void)
{
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        return ws.ws_row;
    return 24;
}

static int term_cols(void)
{
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}

/* ------------------------------------------------------------------ */
/* configuration model                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    char *name;
    char *value;
} cfg_alias;

typedef struct {
    char *prompt;     /* NULL => shell default prompt        */
    char *title;      /* NULL => default title template      */
    int title_off;    /* CSX_TITLE_OFF                       */
    int suggest;      /* CSX_SUGGEST  (default 1)            */
    int highlight;    /* CSX_HIGHLIGHT (default 1)           */
    int beep;         /* CSX_BEEP     (default 1)            */
    int histsize;     /* CSX_HISTSIZE (default 1000)         */
    cfg_alias *aliases;
    size_t nalias;
    size_t alias_cap;
} Config;

#define DEFAULT_HISTSIZE 1000

static void cfg_set_str(char **slot, const char *v)
{
    free(*slot);
    *slot = (v && *v) ? strdup(v) : NULL;
}

static void cfg_init(Config *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->suggest = 1;
    cfg->highlight = 1;
    cfg->beep = 1;
    cfg->histsize = DEFAULT_HISTSIZE;
}

static void cfg_alias_add(Config *cfg, const char *name, const char *value)
{
    size_t i;
    for (i = 0; i < cfg->nalias; i++)
        if (strcmp(cfg->aliases[i].name, name) == 0) {
            free(cfg->aliases[i].value);
            cfg->aliases[i].value = strdup(value ? value : "");
            return;
        }
    if (cfg->nalias >= cfg->alias_cap) {
        size_t nc = cfg->alias_cap ? cfg->alias_cap * 2 : 16;
        cfg_alias *na = realloc(cfg->aliases, nc * sizeof(cfg_alias));
        if (!na)
            return;
        cfg->aliases = na;
        cfg->alias_cap = nc;
    }
    cfg->aliases[cfg->nalias].name = strdup(name);
    cfg->aliases[cfg->nalias].value = strdup(value ? value : "");
    cfg->nalias++;
}

static void cfg_alias_del(Config *cfg, size_t i)
{
    if (i >= cfg->nalias)
        return;
    free(cfg->aliases[i].name);
    free(cfg->aliases[i].value);
    memmove(&cfg->aliases[i], &cfg->aliases[i + 1],
            (cfg->nalias - i - 1) * sizeof(cfg_alias));
    cfg->nalias--;
}

static void cfg_free(Config *cfg)
{
    size_t i;
    free(cfg->prompt);
    free(cfg->title);
    for (i = 0; i < cfg->nalias; i++) {
        free(cfg->aliases[i].name);
        free(cfg->aliases[i].value);
    }
    free(cfg->aliases);
    memset(cfg, 0, sizeof *cfg);
}

/* ------------------------------------------------------------------ */
/* rc file parsing                                                     */
/* ------------------------------------------------------------------ */
static char *trim_ws(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t')
        s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
        *--e = '\0';
    return s;
}

static void strip_quotes(char *v)
{
    size_t n = strlen(v);
    if (n >= 2 && ((v[0] == '\'' && v[n - 1] == '\'') ||
                   (v[0] == '"' && v[n - 1] == '"'))) {
        memmove(v, v + 1, n - 2);
        v[n - 2] = '\0';
    }
}

/* Returns 1 when the `set` line is managed, 0 when it should be
 * preserved as a raw line. */
static int parse_set_line(Config *cfg, const char *rest)
{
    char buf[1024];
    char *eq;
    char *name;
    char *val;

    if (!rest || !*rest)
        return 0;
    snprintf(buf, sizeof(buf), "%s", rest);
    eq = strchr(buf, '=');
    if (!eq)
        return 0;
    *eq = '\0';
    name = trim_ws(buf);
    val = trim_ws(eq + 1);
    strip_quotes(val);

    if (strcmp(name, "CSX_PROMPT") == 0) {
        cfg_set_str(&cfg->prompt, val);
        return 1;
    }
    if (strcmp(name, "CSX_TITLE") == 0) {
        cfg_set_str(&cfg->title, val);
        return 1;
    }
    if (strcmp(name, "CSX_TITLE_OFF") == 0) {
        cfg->title_off = atoi(val) != 0;
        return 1;
    }
    if (strcmp(name, "CSX_SUGGEST") == 0) {
        cfg->suggest = atoi(val) != 0;
        return 1;
    }
    if (strcmp(name, "CSX_HIGHLIGHT") == 0) {
        cfg->highlight = atoi(val) != 0;
        return 1;
    }
    if (strcmp(name, "CSX_BEEP") == 0) {
        cfg->beep = atoi(val) != 0;
        return 1;
    }
    if (strcmp(name, "CSX_HISTSIZE") == 0) {
        int n = atoi(val);
        if (n >= 1)
            cfg->histsize = n;
        return 1;
    }
    return 0;
}

static void parse_alias_line(Config *cfg, const char *rest)
{
    char buf[1024];
    char *eq;
    char *name;
    char *val;

    snprintf(buf, sizeof(buf), "%s", rest);
    eq = strchr(buf, '=');
    if (!eq)
        return;
    *eq = '\0';
    name = trim_ws(buf);
    val = trim_ws(eq + 1);
    strip_quotes(val);
    if (!*name || !*val)
        return;
    cfg_alias_add(cfg, name, val);
}

typedef struct {
    char **lines;
    size_t count;
    size_t cap;
} RawLines;

static void raw_add(RawLines *r, const char *line)
{
    if (r->count >= r->cap) {
        size_t nc = r->cap ? r->cap * 2 : 16;
        char **nl = realloc(r->lines, nc * sizeof(char *));
        if (!nl)
            return;
        r->lines = nl;
        r->cap = nc;
    }
    r->lines[r->count++] = strdup(line);
}

static void raw_free(RawLines *r)
{
    size_t i;
    for (i = 0; i < r->count; i++)
        free(r->lines[i]);
    free(r->lines);
    memset(r, 0, sizeof *r);
}

static const char *rc_path(char *buf, size_t sz)
{
    const char *home = getenv("HOME");
    if (!home)
        home = ".";
    snprintf(buf, sz, "%s/.catshellxrc", home);
    return buf;
}

static void parse_rc(const char *path, Config *cfg, RawLines *raw)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim_ws(line);
        if (!*s || *s == '#') {
            if (*s)
                raw_add(raw, s);
            continue;
        }
        if (strncmp(s, "set ", 4) == 0) {
            if (!parse_set_line(cfg, s + 4))
                raw_add(raw, s);
            continue;
        }
        if (strncmp(s, "alias ", 6) == 0) {
            parse_alias_line(cfg, s + 6);
            continue;
        }
        raw_add(raw, s);
    }
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* rc file writing                                                     */
/* ------------------------------------------------------------------ */
/* Quote a value for the rc: single quotes when possible, double quotes
 * (escaping \ and ") otherwise. */
static void write_quoted(FILE *f, const char *v)
{
    const char *p;
    if (strchr(v, '\'')) {
        fputc('"', f);
        for (p = v; *p; p++) {
            if (*p == '"' || *p == '\\')
                fputc('\\', f);
            fputc(*p, f);
        }
        fputc('"', f);
    } else {
        fprintf(f, "'%s'", v);
    }
}

static int save_rc(Config *cfg, RawLines *raw)
{
    char path[4096];
    char bak[sizeof(path) + 8];
    FILE *f;
    size_t i;

    rc_path(path, sizeof(path));
    snprintf(bak, sizeof(bak), "%s.bak", path);

    remove(bak);
    if (access(path, F_OK) == 0)
        rename(path, bak);

    f = fopen(path, "w");
    if (!f)
        return -1;

    fprintf(f, "# %s\n", "CatShellX configuration (generated by cat_config).");
    fprintf(f, "# Managed by cat_config: %s\n",
            "run `cat_config` to edit; old file kept in ~/.catshellxrc.bak");
    fprintf(f, "# --- managed settings (cat_config) ---\n");

    if (cfg->prompt)
        { fprintf(f, "set CSX_PROMPT="); write_quoted(f, cfg->prompt); fprintf(f, "\n"); }
    if (cfg->title)
        { fprintf(f, "set CSX_TITLE="); write_quoted(f, cfg->title); fprintf(f, "\n"); }
    if (cfg->title_off)
        fprintf(f, "set CSX_TITLE_OFF=1\n");
    if (!cfg->suggest)
        fprintf(f, "set CSX_SUGGEST=0\n");
    if (!cfg->highlight)
        fprintf(f, "set CSX_HIGHLIGHT=0\n");
    if (!cfg->beep)
        fprintf(f, "set CSX_BEEP=0\n");
    if (cfg->histsize != DEFAULT_HISTSIZE)
        fprintf(f, "set CSX_HISTSIZE=%d\n", cfg->histsize);

    for (i = 0; i < cfg->nalias; i++) {
        fprintf(f, "alias %s=", cfg->aliases[i].name);
        write_quoted(f, cfg->aliases[i].value);
        fprintf(f, "\n");
    }

    if (raw->count > 0) {
        fprintf(f, "# --- other settings ---\n");
        for (i = 0; i < raw->count; i++)
            fprintf(f, "%s\n", raw->lines[i]);
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* template escapes (same set as the shell's $CSX_PROMPT)              */
/* ------------------------------------------------------------------ */
static const char *tpl_user(void)
{
    const char *u = getenv("USER");
    return u ? u : "?";
}

static const char *tpl_host(void)
{
    static char host[256];
    if (gethostname(host, sizeof(host)) != 0)
        strcpy(host, "?");
    char *dot = strchr(host, '.');
    if (dot)
        *dot = '\0';
    return host;
}

static void tpl_cwd_short(char *buf, size_t sz)
{
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        snprintf(buf, sz, "?");
        return;
    }
    const char *home = getenv("HOME");
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        if (cwd[strlen(home)] == '\0') {
            snprintf(buf, sz, "~");
            return;
        }
        if (cwd[strlen(home)] == '/') {
            snprintf(buf, sz, "~%s", cwd + strlen(home));
            return;
        }
    }
    snprintf(buf, sz, "%s", cwd);
}

static const char *tpl_cwd_base(char *buf, size_t sz)
{
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        snprintf(buf, sz, "?");
        return buf;
    }
    const char *b = strrchr(cwd, '/');
    snprintf(buf, sz, "%s", b ? b + 1 : cwd);
    return buf;
}

/* Render a prompt/title template into out.  '\n' becomes "^J" so the
 * preview stays on a single line. */
static void render_tpl(const char *tpl, strbuf *out)
{
    size_t i = 0;
    while (tpl[i]) {
        if (tpl[i] == '\\' && tpl[i + 1]) {
            char e = tpl[i + 1];
            char buf[4096];
            char tbuf[64];
            time_t now;
            struct tm *lt;
            switch (e) {
            case 'u': sb_puts(out, tpl_user()); break;
            case 'h': sb_puts(out, tpl_host()); break;
            case 's': sb_puts(out, "catshellx"); break;
            case 'w': tpl_cwd_short(buf, sizeof(buf)); sb_puts(out, buf); break;
            case 'W': sb_puts(out, tpl_cwd_base(buf, sizeof(buf))); break;
            case 'd':
                now = time(NULL);
                lt = localtime(&now);
                if (lt && strftime(tbuf, sizeof(tbuf), "%a %b %d", lt))
                    sb_puts(out, tbuf);
                break;
            case 't':
                now = time(NULL);
                lt = localtime(&now);
                if (lt && strftime(tbuf, sizeof(tbuf), "%H:%M:%S", lt))
                    sb_puts(out, tbuf);
                break;
            case 'n': sb_puts(out, "^J"); break;
            case '$': sb_putc(out, '$'); break;
            case 'e': sb_putc(out, 0x1b); break;
            default: sb_putc(out, e); break;
            }
            i += 2;
            continue;
        }
        sb_putc(out, tpl[i]);
        i++;
    }
}

/* The shell's default fish prompt, expressed as a template. */
static const char *DEFAULT_PROMPT_TPL =
    "\x1b[32m\\u@\\h\x1b[0m \x1b[36m\\w\x1b[0m \x1b[1;33m\xe2\x9d\xaf\x1b[0m ";

/* ------------------------------------------------------------------ */
/* single-line field editor                                            */
/* ------------------------------------------------------------------ */
#define FIELD_MAX 512

typedef struct {
    char buf[FIELD_MAX];
    int len;
    int pos;
} Field;

static void field_set(Field *f, const char *s)
{
    f->len = (int)strlen(s);
    if (f->len > FIELD_MAX - 1)
        f->len = FIELD_MAX - 1;
    memcpy(f->buf, s, (size_t)f->len);
    f->buf[f->len] = '\0';
    f->pos = f->len;
}

static void field_insert(Field *f, int c)
{
    if (f->len >= FIELD_MAX - 1)
        return;
    memmove(f->buf + f->pos + 1, f->buf + f->pos, (size_t)(f->len - f->pos) + 1);
    f->buf[f->pos] = (char)c;
    f->pos++;
    f->len++;
}

static void field_del_back(Field *f)
{
    if (f->pos == 0)
        return;
    memmove(f->buf + f->pos - 1, f->buf + f->pos, (size_t)(f->len - f->pos) + 1);
    f->pos--;
    f->len--;
}

static void field_del_fwd(Field *f)
{
    if (f->pos >= f->len)
        return;
    memmove(f->buf + f->pos, f->buf + f->pos + 1, (size_t)(f->len - f->pos));
    f->len--;
}

static void draw_field(const char *title, const char *label, Field *f)
{
    int cols = term_cols();
    int pl = (int)strlen(label) + 2;
    int avail = cols - pl - 2;
    int start = 0;
    int vis;

    if (avail < 4)
        avail = 4;
    if (f->len > avail && f->pos > avail)
        start = f->pos - avail;
    vis = f->len - start;
    if (vis > avail)
        vis = avail;

    cls();
    printf(C_CYAN "%s" C_RESET "\n\n", title);
    printf("%s: ", label);
    fwrite(f->buf + start, 1, (size_t)vis, stdout);
    fputs("\x1b[K", stdout);
    gotoxy(3, pl + (f->pos - start) + 1);
    printf("\n\n%sEnter: confirm   Esc: cancel%s", C_DIM, C_RESET);
    fflush(stdout);
}

/* returns 1 on Enter, 0 on Esc/Ctrl-C */
static int edit_field(const char *title, const char *label, Field *f)
{
    for (;;) {
        draw_field(title, label, f);
        int k = read_key();
        if (k == K_ENTER)
            return 1;
        if (k == K_ESC || k == 0x03 || k == K_READERR)
            return 0;
        if (k == 0x01) f->pos = 0;
        else if (k == 0x05) f->pos = f->len;
        else if (k == 0x02) { if (f->pos > 0) f->pos--; }
        else if (k == 0x06) { if (f->pos < f->len) f->pos++; }
        else if (k == 0x15) { f->pos = 0; f->len = 0; f->buf[0] = '\0'; }
        else if (k == 0x0b) { f->buf[f->pos] = '\0'; f->len = f->pos; }
        else if (k == K_LEFT) { if (f->pos > 0) f->pos--; }
        else if (k == K_RIGHT) { if (f->pos < f->len) f->pos++; }
        else if (k == K_HOME) f->pos = 0;
        else if (k == K_END) f->pos = f->len;
        else if (k == K_DEL) field_del_fwd(f);
        else if (k == K_BACKSPACE || k == '\b') field_del_back(f);
        else if (k >= 0x20 && k < 0x7f) field_insert(f, k);
    }
}

/* ------------------------------------------------------------------ */
/* list drawing / navigation helpers                                   */
/* ------------------------------------------------------------------ */
static void draw_list(const char *title, const char *const *items, int n, int sel,
                      int *top, const char *help)
{
    int rows = term_rows();
    int cols = term_cols();
    int visible = rows - 5;
    int lim = cols - 3;
    int i;

    if (lim > 400)
        lim = 400;
    if (lim < 1)
        lim = 1;
    if (*top > sel) *top = sel;
    if (sel >= *top + visible) *top = sel - visible + 1;
    if (visible > n) *top = 0;

    cls();
    printf(C_CYAN "%s" C_RESET "\n", title);
    for (i = *top; i < n && i < *top + visible; i++) {
        char buf[512];
        int row = 3 + (i - *top);
        strncpy(buf, items[i] ? items[i] : "", (size_t)lim);
        buf[lim] = '\0';
        gotoxy(row, 1);
        printf("%s%s%s%s", i == sel ? "> " : "  ",
               i == sel ? C_YELLOW : "",
               buf,
               i == sel ? C_RESET : "");
        fputs("\x1b[K", stdout);
    }
    gotoxy(rows, 1);
    printf("%s%s%s", C_DIM, help, C_RESET);
    fputs("\x1b[K", stdout);
    fflush(stdout);
}

/* Replace raw ESC/control bytes with visible text so templates can be
 * shown safely inside the list. */
static void sanitize_desc(const char *tpl, char *out, size_t sz)
{
    size_t o = 0, i;
    for (i = 0; tpl[i] && o + 2 < sz; i++) {
        unsigned char c = (unsigned char)tpl[i];
        if (c == 0x1b) {
            out[o++] = '\\';
            out[o++] = 'e';
        } else if (c == '\n') {
            out[o++] = '^';
            out[o++] = 'J';
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

/* ------------------------------------------------------------------ */
/* screens                                                             */
/* ------------------------------------------------------------------ */

enum { M_PROMPT, M_TITLE, M_ALIAS, M_BEHAV, M_SAVE, M_QUIT, M_COUNT };

static int run_prompt_screen(Config *cfg)
{
    const char *presets[5] = {
        "Default fish:  user@host cwd \xe2\x9d\xaf (colors)",
        "Minimal:        \\w> ",
        "Bash-like:      \\u@\\h \\w \\$ ",
        "Fish colored:   \\e[32m\\u@\\h\\e[0m ...",
        "Custom template (type your own)..."
    };
    int sel = 0, top = 0;
    const char *items[6];
    char defitem[160];

    for (;;) {
        sanitize_desc(cfg->prompt ? cfg->prompt : DEFAULT_PROMPT_TPL,
                      defitem, sizeof(defitem));
        items[0] = presets[0];
        items[1] = presets[1];
        items[2] = presets[2];
        items[3] = presets[3];
        items[4] = presets[4];
        items[5] = defitem;
        draw_list("Prompt - choose a preset (Enter) or a custom template", items, 6,
                  sel, &top, "j/k or arrows: navigate   Enter: select   Esc: back");

        gotoxy(term_rows() - 1, 1);
        printf(C_DIM "Preview: " C_RESET);
        fflush(stdout);
        {
            strbuf p;
            const char *tpl;
            sb_init(&p);
            if (sel == 1) tpl = "\\w> ";
            else if (sel == 2) tpl = "\\u@\\h \\w \\$ ";
            else if (sel == 4)
                tpl = (cfg->prompt && strcmp(cfg->prompt, DEFAULT_PROMPT_TPL) != 0)
                      ? cfg->prompt : "";
            else if (sel == 5) tpl = cfg->prompt ? cfg->prompt : DEFAULT_PROMPT_TPL;
            else tpl = DEFAULT_PROMPT_TPL;
            render_tpl(tpl, &p);
            fputs(sb_str(&p), stdout);
            sb_free(&p);
        }
        fputs("\x1b[K", stdout);
        fflush(stdout);

        int k = read_key();
        if (k == K_UP || k == 'k') { if (sel > 0) sel--; }
        else if (k == K_DOWN || k == 'j') { if (sel < 5) sel++; }
        else if (k == K_ESC || k == 0x03) return 0;
        else if (k == K_ENTER || k == '\n') {
            if (sel == 0) cfg_set_str(&cfg->prompt, NULL);
            else if (sel == 1) cfg_set_str(&cfg->prompt, "\\w> ");
            else if (sel == 2) cfg_set_str(&cfg->prompt, "\\u@\\h \\w \\$ ");
            else if (sel == 3) cfg_set_str(&cfg->prompt, DEFAULT_PROMPT_TPL);
            else if (sel == 4) {
                Field f;
                const char *cur = cfg->prompt;
                field_set(&f, (cur && strcmp(cur, DEFAULT_PROMPT_TPL) != 0) ? cur : "");
                if (edit_field("Prompt - custom template", "Template", &f) && f.len > 0)
                    cfg_set_str(&cfg->prompt, f.buf);
                else if (f.len == 0)
                    cfg_set_str(&cfg->prompt, NULL);
            }
            return 1; /* back to main */
        }
    }
}

static int run_title_screen(Config *cfg)
{
    int sel = 0, top = 0;
    int changed = 0;
    for (;;) {
        char items[2][192];
        char tdesc[160];
        sanitize_desc(cfg->title ? cfg->title : "\\u@\\h: \\w", tdesc, sizeof(tdesc));
        snprintf(items[0], sizeof(items[0]), "Window title:           [%s]",
                 cfg->title_off ? "OFF" : "ON");
        snprintf(items[1], sizeof(items[1]), "Template:               %s", tdesc);
        char *it[2] = { items[0], items[1] };
        draw_list("Window title (OSC 0)", (const char *const *)it, 2, sel, &top,
                  "j/k: navigate   Enter: toggle/edit   Esc: back");

        gotoxy(term_rows() - 1, 1);
        printf(C_DIM "Preview: " C_RESET);
        fflush(stdout);
        {
            strbuf t;
            sb_init(&t);
            if (cfg->title_off) {
                fputs(C_DIM "(disabilitato)" C_RESET, stdout);
            } else {
                render_tpl(cfg->title ? cfg->title : "\\u@\\h: \\w", &t);
                fputs(sb_str(&t), stdout);
            }
            sb_free(&t);
        }
        fputs("\x1b[K", stdout);
        fflush(stdout);

        int k = read_key();
        if (k == K_UP || k == 'k') { if (sel > 0) sel--; }
        else if (k == K_DOWN || k == 'j') { if (sel < 1) sel++; }
        else if (k == K_ESC || k == 0x03) return changed;
        else if (k == K_ENTER || k == '\n') {
            if (sel == 0) {
                cfg->title_off = !cfg->title_off;
                changed = 1;
            } else {
                Field f;
                field_set(&f, cfg->title ? cfg->title : "");
                if (edit_field("Title - custom template", "Template", &f)) {
                    cfg_set_str(&cfg->title, f.len > 0 ? f.buf : NULL);
                    changed = 1;
                }
            }
        }
    }
}

static int run_alias_screen(Config *cfg)
{
    int sel = 0, top = 0;
    int changed = 0;
    for (;;) {
        int n = (int)cfg->nalias + 1; /* + "add" row */
        char **items = calloc((size_t)n + 1, sizeof(char *));
        char *rowbuf = calloc((size_t)n, sizeof(char) * 512);
        size_t i;
        if (!items || !rowbuf) {
            free(items);
            free(rowbuf);
            return changed;
        }
        for (i = 0; i < cfg->nalias; i++) {
            snprintf(&rowbuf[i * 512], 512, "%s = %s",
                     cfg->aliases[i].name, cfg->aliases[i].value);
            items[i] = &rowbuf[i * 512];
        }
        snprintf(&rowbuf[i * 512], 512, "[Add new alias]");
        items[i] = &rowbuf[i * 512];
        items[n] = NULL;

        draw_list("Aliases", (const char *const *)items, n, sel, &top,
                  "j/k: navigate   Enter: edit   d: delete   Esc: back");

        int k = read_key();
        if (k == K_UP || k == 'k') { if (sel > 0) sel--; }
        else if (k == K_DOWN || k == 'j') { if (sel < n - 1) sel++; }
        else if (k == K_ESC || k == 0x03) { free(items); free(rowbuf); return changed; }
        else if (k == 'd' || k == 'x' || k == K_DEL) {
            if (sel < (int)cfg->nalias) {
                cfg_alias_del(cfg, (size_t)sel);
                if (sel > (int)cfg->nalias)
                    sel = (int)cfg->nalias;
                changed = 1;
            }
        }
        else if (k == K_ENTER || k == '\n' || k == 'a') {
            Field f;
            char name[FIELD_MAX];
            if (sel < (int)cfg->nalias) {
                /* edit existing */
                strncpy(name, cfg->aliases[sel].name, sizeof(name) - 1);
                name[sizeof(name) - 1] = '\0';
                field_set(&f, cfg->aliases[sel].value);
                if (edit_field("Alias - value", name, &f) && f.len > 0) {
                    cfg_alias_add(cfg, name, f.buf);
                    changed = 1;
                }
            } else {
                /* add new */
                field_set(&f, "");
                if (edit_field("New alias", "Name", &f) && f.len > 0) {
                    strncpy(name, f.buf, sizeof(name) - 1);
                    name[sizeof(name) - 1] = '\0';
                    field_set(&f, "");
                    if (edit_field("New alias", "Value", &f) && f.len > 0) {
                        cfg_alias_add(cfg, name, f.buf);
                        changed = 1;
                    }
                }
            }
        }
        free(items);
        free(rowbuf);
    }
}

static int run_behavior_screen(Config *cfg)
{
    int sel = 0, top = 0;
    int changed = 0;
    for (;;) {
        char items[5][128];
        char *it[5];
        int i;
        snprintf(items[0], sizeof(items[0]), "Autosuggestions          [%s]", cfg->suggest ? "ON" : "OFF");
        snprintf(items[1], sizeof(items[1]), "Syntax highlighting      [%s]", cfg->highlight ? "ON" : "OFF");
        snprintf(items[2], sizeof(items[2]), "Beeper (bell on errors)  [%s]", cfg->beep ? "ON" : "OFF");
        snprintf(items[3], sizeof(items[3]), "Window title             [%s]", cfg->title_off ? "OFF" : "ON");
        snprintf(items[4], sizeof(items[4]), "History size             [%d]", cfg->histsize);
        for (i = 0; i < 5; i++)
            it[i] = items[i];

        draw_list("Behavior - Enter: toggle", (const char *const *)it, 5, sel, &top,
                  "j/k: navigate   Enter or Space: toggle   Esc: back");

        int k = read_key();
        if (k == K_UP || k == 'k') { if (sel > 0) sel--; }
        else if (k == K_DOWN || k == 'j') { if (sel < 4) sel++; }
        else if (k == K_ESC || k == 0x03) return changed;
        else if (k == K_ENTER || k == '\n' || k == ' ') {
            switch (sel) {
            case 0: cfg->suggest = !cfg->suggest; changed = 1; break;
            case 1: cfg->highlight = !cfg->highlight; changed = 1; break;
            case 2: cfg->beep = !cfg->beep; changed = 1; break;
            case 3: cfg->title_off = !cfg->title_off; changed = 1; break;
            case 4: {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", cfg->histsize);
                Field f;
                field_set(&f, buf);
                if (edit_field("History size", "Number of entries (1-1000)", &f) && f.len > 0) {
                    int n = atoi(f.buf);
                    if (n < 1) n = 1;
                    if (n > 1000) n = 1000;
                    cfg->histsize = n;
                    changed = 1;
                }
                break;
            }
            }
        }
    }
}

/* returns 1 = save & exit, 2 = exit without saving, 0 = cancel */
static int run_confirm_screen(void)
{
    cls();
    printf(C_YELLOW "Configuration changed." C_RESET "\n\n");
    printf("  [s] Save and exit\n");
    printf("  [x] Exit without saving\n");
    printf("  [c] Cancel\n\n");
    fflush(stdout);
    for (;;) {
        int k = read_key();
        if (k == 's')
            return 1;
        if (k == 'x' || k == K_ESC)
            return 2;
        if (k == 'c')
            return 0;
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    Config cfg;
    RawLines raw;
    char path[4096];
    int dirty = 0;
    int quit = 0;
    int saved = 0;

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "cat_config: needs an interactive terminal\n");
        return 1;
    }

    cfg_init(&cfg);
    memset(&raw, 0, sizeof raw);
    rc_path(path, sizeof(path));
    parse_rc(path, &cfg, &raw);

    if (raw_mode(1) != 0) {
        fprintf(stderr, "cat_config: cannot enter raw mode\n");
        cfg_free(&cfg);
        raw_free(&raw);
        return 1;
    }

    int sel = 0, top = 0;
    while (!quit) {
        char *items[M_COUNT] = {
            "Prompt (live preview)",
            "Window title",
            "Aliases",
            "Behavior",
            "Save and exit",
            "Exit without saving",
        };
        draw_list("CatShellX Configurator", (const char *const *)items, M_COUNT, sel, &top,
                  "j/k or arrows: navigate   Enter: select   Esc: exit");

        int k = read_key();
        if (k == K_UP || k == 'k') { if (sel > 0) sel--; }
        else if (k == K_DOWN || k == 'j') { if (sel < M_COUNT - 1) sel++; }
        else if (k == K_ESC || k == 0x03) {
            if (!dirty) {
                quit = 1;
            } else {
                int r = run_confirm_screen();
                if (r == 1 && save_rc(&cfg, &raw) == 0) {
                    saved = 1;
                    quit = 1;
                } else if (r == 2) {
                    quit = 1;
                }
            }
        }
        else if (k == K_ENTER || k == '\n') {
            if (sel == M_PROMPT) {
                if (run_prompt_screen(&cfg)) dirty = 1;
            } else if (sel == M_TITLE) {
                if (run_title_screen(&cfg)) dirty = 1;
            } else if (sel == M_ALIAS) {
                if (run_alias_screen(&cfg)) dirty = 1;
            } else if (sel == M_BEHAV) {
                if (run_behavior_screen(&cfg)) dirty = 1;
            } else if (sel == M_SAVE) {
                if (save_rc(&cfg, &raw) == 0) {
                    saved = 1;
                    quit = 1;
                } else {
                    fprintf(stderr, "\ncat_config: cannot write %s\n", path);
                }
            } else if (sel == M_QUIT) {
                if (!dirty) {
                    quit = 1;
                } else {
                    int r = run_confirm_screen();
                    if (r == 1 && save_rc(&cfg, &raw) == 0) {
                        saved = 1;
                        quit = 1;
                    } else if (r == 2) {
                        quit = 1;
                    }
                }
            }
        }
    }

    raw_mode(0);
    cls();
    if (saved) {
        printf(C_GREEN "Configuration saved to %s%s\n", path, C_RESET);
        printf(C_DIM "Backup of the previous version is at %s.bak\n"
                     "Changes apply to the next shell you start.%s\n",
               path, C_RESET);
    } else if (dirty) {
        printf(C_DIM "No changes saved.%s\n", C_RESET);
    }

    cfg_free(&cfg);
    raw_free(&raw);
    return 0;
}
