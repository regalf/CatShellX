#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>

static struct termios orig_tio;
static int raw_on = 0;

int csx_eof = 0;
int csx_cancelled = 0;

int csx_raw_mode(int on)
{
    if (on && !raw_on) {
        if (tcgetattr(0, &orig_tio) != 0)
            return -1;
        struct termios t = orig_tio;
        t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
        t.c_iflag &= ~(IXON | ICRNL | INLCR);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        if (tcsetattr(0, TCSANOW, &t) != 0)
            return -1;
        raw_on = 1;
    } else if (!on && raw_on) {
        tcsetattr(0, TCSANOW, &orig_tio);
        raw_on = 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* virtual key codes (negative => escape sequences)                    */
/* ------------------------------------------------------------------ */
#define K_UP           -1
#define K_DOWN         -2
#define K_RIGHT        -3
#define K_LEFT         -4
#define K_HOME         -5
#define K_END          -6
#define K_DEL          -7
#define K_ALT_LEFT     -8
#define K_ALT_RIGHT    -9
#define K_ALT_BACKSPACE -10
#define K_READ_ERR     -11
#define K_ALT_D        -12
#define K_ALT_Y        -13
#define K_IGNORE       -14

#define ED_MAXLEN 1024

static int read_byte(void)
{
    unsigned char c;
    ssize_t n;
    while ((n = read(0, &c, 1)) < 0 && errno == EINTR)
        ;
    if (n <= 0)
        return K_READ_ERR;
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
        return 0x1b;

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
        case 'b': return K_ALT_LEFT;
        case 'f': return K_ALT_RIGHT;
        case '1':
        case '7': { int c4 = read_byte(); if (c4 == '~') return K_HOME; return c3; }
        case '3': { int c4 = read_byte(); if (c4 == '~') return K_DEL; return c3; }
        case '4':
        case '8': { int c4 = read_byte(); if (c4 == '~') return K_END; return c3; }
        default: return c3;
        }
    }
    if (c2 == 0x7f)
        return K_ALT_BACKSPACE;
    if (c2 == 'b')
        return K_ALT_LEFT;
    if (c2 == 'f')
        return K_ALT_RIGHT;
    if (c2 == 'd')
        return K_ALT_D;
    if (c2 == 'y')
        return K_ALT_Y;
    return K_IGNORE;
}

/* ------------------------------------------------------------------ */
/* editor buffer                                                       */
/* ------------------------------------------------------------------ */
#define UNDO_MAX 128

typedef struct {
    char *text;
    size_t pos;
} ed_snap;

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    size_t pos;
    const char *prompt;
    size_t pw;
    int tab_state;
    ed_snap *undo;
    int nundo;
    int undocap;
    int undoidx;
    int undo_toggle;
    char **kills;
    int nkills;
    int killcap;
    int killidx;
    size_t yank_base;
    size_t yank_len;
    int yank_idx;
} editor;

static void ed_ensure(editor *e, size_t extra)
{
    size_t need = e->len + extra + 1;
    if (need <= e->cap)
        return;
    size_t cap = e->cap ? e->cap : 128;
    while (cap < need)
        cap *= 2;
    char *nb = realloc(e->buf, cap);
    if (!nb)
        return;
    e->buf = nb;
    e->cap = cap;
}

static void mark_edited(editor *e);

static void ed_free(editor *e)
{
    int i;
    for (i = 0; i < e->nundo; i++)
        free(e->undo[i].text);
    free(e->undo);
    for (i = 0; i < e->nkills; i++)
        free(e->kills[i]);
    free(e->kills);
    free(e->buf);
}

static void set_buffer(editor *e, const char *s)
{
    size_t n = strlen(s);
    ed_ensure(e, n);
    memcpy(e->buf, s, n + 1);
    e->len = n;
    e->pos = n;
}

static void ed_append(editor *e, const char *s)
{
    size_t n = strlen(s);
    ed_ensure(e, n);
    memcpy(e->buf + e->len, s, n + 1);
    e->len += n;
    e->pos = e->len;
    mark_edited(e);
}

static void mark_edited(editor *e)
{
    hist_nav_reset();
    e->undo_toggle = 0;
    if (e->nundo > 0 && e->undoidx == e->nundo - 1 &&
        e->undo[e->undoidx].pos == e->pos &&
        strcmp(e->undo[e->undoidx].text, e->buf) == 0)
        return;
    while (e->nundo > e->undoidx + 1)
        free(e->undo[--e->nundo].text);
    if (e->nundo >= e->undocap) {
        int nc = e->undocap ? e->undocap * 2 : 16;
        ed_snap *ns = realloc(e->undo, (size_t)nc * sizeof(ed_snap));
        if (!ns)
            return;
        e->undo = ns;
        e->undocap = nc;
    }
    if (e->nundo >= UNDO_MAX) {
        free(e->undo[0].text);
        memmove(&e->undo[0], &e->undo[1],
                (size_t)(e->nundo - 1) * sizeof(ed_snap));
        e->nundo--;
        e->undoidx--;
    }
    e->undo[e->nundo].text = strdup(e->buf);
    if (!e->undo[e->nundo].text)
        return;
    e->undo[e->nundo].pos = e->pos;
    e->nundo++;
    e->undoidx = e->nundo - 1;
}

static int undo_apply(editor *e, int dir)
{
    int ni = e->undoidx + dir;
    if (ni < 0 || ni >= e->nundo)
        return 0;
    e->undoidx = ni;
    size_t n = strlen(e->undo[ni].text);
    ed_ensure(e, n);
    memcpy(e->buf, e->undo[ni].text, n + 1);
    e->len = n;
    e->pos = e->undo[ni].pos;
    return 1;
}

/* ------------------------------------------------------------------ */
/* kill ring                                                           */
/* ------------------------------------------------------------------ */
static void kill_push(editor *e, const char *s, size_t n)
{
    char tmp[ED_MAXLEN + 1];
    if (n == 0)
        return;
    if (n > ED_MAXLEN)
        n = ED_MAXLEN;
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    if (e->killidx >= 0 && e->killidx < e->nkills &&
        strcmp(e->kills[e->killidx], tmp) == 0)
        return;
    if (e->nkills >= e->killcap) {
        int nc = e->killcap ? e->killcap * 2 : 8;
        char **nk = realloc(e->kills, (size_t)nc * sizeof(char *));
        if (!nk)
            return;
        e->kills = nk;
        e->killcap = nc;
    }
    char *dup = strdup(tmp);
    if (!dup)
        return;
    e->kills[e->nkills++] = dup;
    e->killidx = e->nkills - 1;
    e->yank_idx = e->killidx;
}

static void yank(editor *e)
{
    const char *txt;
    size_t tl;
    if (e->killidx < 0 || e->killidx >= e->nkills)
        return;
    txt = e->kills[e->killidx];
    tl = strlen(txt);
    ed_ensure(e, tl);
    memmove(e->buf + e->pos + tl, e->buf + e->pos, e->len - e->pos + 1);
    memcpy(e->buf + e->pos, txt, tl);
    e->pos += tl;
    e->len += tl;
    e->yank_base = e->pos - tl;
    e->yank_len = tl;
    e->yank_idx = e->killidx;
    mark_edited(e);
}

static void yank_older(editor *e)
{
    int next;
    size_t base;
    if (e->nkills == 0)
        return;
    next = e->yank_idx - 1;
    if (next < 0)
        next = e->nkills - 1;
    base = e->yank_base;
    if (base + e->yank_len <= e->len) {
        size_t tail = e->len - (base + e->yank_len);
        memmove(e->buf + base, e->buf + base + e->yank_len, tail + 1);
        e->len -= e->yank_len;
        e->pos = base;
    }
    {
        const char *txt = e->kills[next];
        size_t tl = strlen(txt);
        ed_ensure(e, tl);
        memmove(e->buf + base + tl, e->buf + base, e->len - base + 1);
        memcpy(e->buf + base, txt, tl);
        e->len += tl;
        e->pos = base + tl;
    }
    e->yank_base = base;
    e->yank_len = strlen(e->kills[next]);
    e->yank_idx = next;
    mark_edited(e);
}

static void ed_kill_word_fwd(editor *e)
{
    size_t i = e->pos;
    size_t start = i;
    while (i < e->len && isspace((unsigned char)e->buf[i]))
        i++;
    while (i < e->len && !isspace((unsigned char)e->buf[i]))
        i++;
    if (i > start) {
        kill_push(e, e->buf + start, i - start);
        memmove(e->buf + start, e->buf + i, e->len - i + 1);
        e->len -= i - start;
        mark_edited(e);
    }
}

static void ed_transpose(editor *e)
{
    char t;
    if (e->len < 2 || e->pos == 0)
        return;
    if (e->pos < e->len) {
        t = e->buf[e->pos - 1];
        e->buf[e->pos - 1] = e->buf[e->pos];
        e->buf[e->pos] = t;
        e->pos++;
    } else {
        t = e->buf[e->len - 2];
        e->buf[e->len - 2] = e->buf[e->len - 1];
        e->buf[e->len - 1] = t;
    }
    mark_edited(e);
}

static void ed_insert(editor *e, int c)
{
    ed_ensure(e, 1);
    memmove(e->buf + e->pos + 1, e->buf + e->pos, e->len - e->pos);
    e->buf[e->pos] = (char)c;
    e->pos++;
    e->len++;
    e->buf[e->len] = '\0';
    mark_edited(e);
}

static void ed_del_back(editor *e)
{
    if (e->pos == 0)
        return;
    memmove(e->buf + e->pos - 1, e->buf + e->pos, e->len - e->pos + 1);
    e->pos--;
    e->len--;
    mark_edited(e);
}

static void ed_del_fwd(editor *e)
{
    if (e->pos >= e->len)
        return;
    memmove(e->buf + e->pos, e->buf + e->pos + 1, e->len - e->pos);
    e->len--;
    mark_edited(e);
}

static void ed_kill_word_back(editor *e)
{
    size_t i = e->pos;
    while (i > 0 && isspace((unsigned char)e->buf[i - 1]))
        i--;
    while (i > 0 && !isspace((unsigned char)e->buf[i - 1]))
        i--;
    if (i < e->pos) {
        kill_push(e, e->buf + i, e->pos - i);
        memmove(e->buf + i, e->buf + e->pos, e->len - e->pos + 1);
        e->len -= e->pos - i;
        e->pos = i;
        mark_edited(e);
    }
}

static void move_word_left(editor *e)
{
    size_t i = e->pos;
    while (i > 0 && !isspace((unsigned char)e->buf[i - 1]))
        i--;
    while (i > 0 && isspace((unsigned char)e->buf[i - 1]))
        i--;
    e->pos = i;
}

static void move_word_right(editor *e)
{
    size_t i = e->pos;
    while (i < e->len && isspace((unsigned char)e->buf[i]))
        i++;
    while (i < e->len && !isspace((unsigned char)e->buf[i]))
        i++;
    e->pos = i;
}

/* ------------------------------------------------------------------ */
/* Tab completion                                                      */
/* ------------------------------------------------------------------ */
static void replace_word(editor *e, size_t base, const char *s)
{
    size_t sl = strlen(s);
    size_t oldlen = e->pos - base;
    size_t tail = e->len - e->pos;
    ed_ensure(e, sl + tail);
    memmove(e->buf + base + sl, e->buf + e->pos, tail + 1);
    memcpy(e->buf + base, s, sl);
    e->len = e->len - oldlen + sl;
    e->pos = base + sl;
    e->buf[e->len] = '\0';
    mark_edited(e);
}

static void common_prefix(char *const *items, size_t n, char *out, size_t outsz)
{
    if (n == 0) {
        out[0] = '\0';
        return;
    }
    size_t i = 0;
    const char *first = items[0];
    while (first[i] && i + 1 < outsz) {
        char ch = first[i];
        size_t j;
        for (j = 1; j < n; j++) {
            if (tolower((unsigned char)items[j][i]) != tolower((unsigned char)ch))
                break;
        }
        if (j != n)
            break;
        i++;
    }
    memcpy(out, first, i);
    out[i] = '\0';
}

static void show_menu(char *const *items, size_t n)
{
    printf("\r\n");
    struct winsize ws;
    int cols = 80;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        cols = ws.ws_col;
    size_t maxw = 1;
    size_t i;
    for (i = 0; i < n; i++) {
        size_t w = strlen(items[i]);
        if (w > maxw)
            maxw = w;
    }
    size_t per = cols / (maxw + 2);
    if (per < 1)
        per = 1;
    for (i = 0; i < n; i++) {
        printf("%-*s", (int)(maxw + 2), items[i]);
        if ((i + 1) % per == 0 || i + 1 == n)
            printf("\r\n");
    }
    fflush(stdout);
}

static void do_tab_complete(editor *e)
{
    csx_completion c;
    if (!csx_complete_line(e->buf, e->pos, &c))
        return;

    if (c.count == 0) {
        if (csx_bool_var("CSX_BEEP", 1))
            fputc('\a', stdout);
        fflush(stdout);
        csx_completion_free(&c);
        return;
    }

    if (c.count == 1) {
        replace_word(e, c.base, c.items[0]);
        e->tab_state = 0;
        csx_completion_free(&c);
        return;
    }

    char lcp[ED_MAXLEN + 1];
    common_prefix(c.items, c.count, lcp, sizeof(lcp));
    size_t wordlen = e->pos - c.base;

    if (strlen(lcp) > wordlen) {
        replace_word(e, c.base, lcp);
        e->tab_state = 1;
    } else {
        show_menu(c.items, c.count);
        e->tab_state = 1;
    }
    csx_completion_free(&c);
}

static void render(editor *e, const char *sug)
{
    strbuf o;
    sb_init(&o);
    sb_printf(&o, "\r%s", e->prompt);
    if (e->len > 0) {
        if (csx_bool_var("CSX_HIGHLIGHT", 1)) {
            strbuf h;
            sb_init(&h);
            csx_highlight(e->buf, &h);
            sb_puts(&o, sb_str(&h));
            sb_free(&h);
        } else {
            sb_puts(&o, e->buf);
        }
    }
    if (sug && *sug) {
        sb_puts(&o, CSX_C_DIM);
        sb_puts(&o, sug);
        sb_puts(&o, CSX_C_RESET);
    }
    sb_puts(&o, "\x1b[K");
    size_t suglen = (sug && *sug) ? strlen(sug) : 0;
    size_t total = e->pw + e->len + suglen;
    size_t cur = e->pw + e->pos;
    if (total > cur) {
        size_t back = total - cur;
        sb_printf(&o, "\x1b[%zuD", back);
    }
    fwrite(o.buf, 1, o.len, stdout);
    fflush(stdout);
    sb_free(&o);
}

/* ------------------------------------------------------------------ */
/* Ctrl-R incremental reverse search                                   */
/* ------------------------------------------------------------------ */
static char *do_reverse_search(editor *e)
{
    strbuf q;
    sb_init(&q);
    const char *match = NULL;

    for (;;) {
        strbuf o;
        sb_init(&o);
        sb_printf(&o, "\r(reverse-i-search)`%s': ", sb_str(&q));
        if (match)
            sb_puts(&o, match);
        sb_puts(&o, "\x1b[K");
        fwrite(o.buf, 1, o.len, stdout);
        fflush(stdout);
        sb_free(&o);

        int k = read_key();
        if (k == 0x12) {
            if (q.len && match)
                match = hist_search_older(sb_str(&q));
            continue;
        }
        if (k == 0x15) {
            sb_reset(&q);
            match = NULL;
            continue;
        }
        if (k == '\x7f' || k == '\b') {
            if (q.len) {
                q.buf[--q.len] = '\0';
                match = q.len ? hist_search(sb_str(&q)) : NULL;
            }
            continue;
        }
        if (k == 0x03 || k == 0x07) {
            sb_free(&q);
            return NULL;
        }
        if (k == '\r' || k == '\n') {
            if (match) {
                char *m = strdup(match);
                sb_free(&q);
                return m;
            }
            continue;
        }
        if (k >= 0x20 && k < 0x7f) {
            sb_putc(&q, k);
            match = hist_search(sb_str(&q));
            continue;
        }
    }
}

/* ------------------------------------------------------------------ */
/* main readline                                                       */
/* ------------------------------------------------------------------ */
char *csx_readline(const char *prompt, size_t prompt_width,
                   const char *(*suggest)(const char *))
{
    csx_eof = 0;
    csx_cancelled = 0;

    editor e;
    memset(&e, 0, sizeof(e));
    e.cap = 128;
    e.buf = malloc(e.cap);
    e.buf[0] = '\0';
    e.prompt = prompt;
    e.pw = prompt_width;
    mark_edited(&e);

    for (;;) {
        const char *s = (suggest && e.pos == e.len) ? suggest(e.buf) : NULL;
        render(&e, s);

        int k = read_key();
        if (k != 0x09)
            e.tab_state = 0;
        if (k == K_READ_ERR)
            break;
        if (k == '\r' || k == '\n') {
            fputs("\n", stdout);
            fflush(stdout);
            char *out = strdup(e.buf);
            ed_free(&e);
            return out;
        }
        if (k == 0x04) {
            if (e.len == 0) {
                ed_free(&e);
                csx_eof = 1;
                return NULL;
            }
            ed_del_fwd(&e);
            continue;
        }
        if (k == 0x03) {
            ed_free(&e);
            csx_cancelled = 1;
            return NULL;
        }
        if (k == 0x01) e.pos = 0;
        else if (k == 0x05) e.pos = e.len;
        else if (k == 0x02) { if (e.pos > 0) e.pos--; }
        else if (k == 0x06) {
            if (e.pos < e.len)
                e.pos++;
            else if (s && *s)
                ed_append(&e, s);
        }
        else if (k == 0x0b) {
            kill_push(&e, e.buf + e.pos, e.len - e.pos);
            e.len = e.pos;
            e.buf[e.len] = '\0';
            mark_edited(&e);
        }
        else if (k == 0x15) {
            kill_push(&e, e.buf, e.pos);
            memmove(e.buf, e.buf + e.pos, e.len - e.pos + 1);
            e.len -= e.pos;
            e.pos = 0;
            mark_edited(&e);
        }
        else if (k == 0x17) ed_kill_word_back(&e);
        else if (k == 0x0c) printf("\x1b[2J\x1b[H");
        else if (k == 0x12) {
            char *m = do_reverse_search(&e);
            if (m) {
                free(e.buf);
                e.buf = m;
                e.cap = strlen(m) + 1;
                e.len = strlen(m);
                e.pos = e.len;
                mark_edited(&e);
            }
            continue;
        }
        else if (k == 0x09) {
            do_tab_complete(&e);
            continue;
        }
        else if (k == 0x14) ed_transpose(&e);
        else if (k == 0x19) yank(&e);
        else if (k == 0x1f) {
            int dir = e.undo_toggle ? 1 : -1;
            if (undo_apply(&e, dir))
                e.undo_toggle = !e.undo_toggle;
        }
        else if (k == K_UP) {
            const char *h = hist_nav_prev();
            if (h) {
                set_buffer(&e, h);
                mark_edited(&e);
            }
        }
        else if (k == K_DOWN) {
            const char *h = hist_nav_next();
            if (h) {
                set_buffer(&e, h);
                mark_edited(&e);
            } else
                set_buffer(&e, "");
        }
        else if (k == K_RIGHT) {
            if (e.pos < e.len)
                e.pos++;
            else if (s && *s)
                ed_append(&e, s);
        }
        else if (k == K_LEFT) { if (e.pos > 0) e.pos--; }
        else if (k == K_HOME) e.pos = 0;
        else if (k == K_END) e.pos = e.len;
        else if (k == K_DEL) ed_del_fwd(&e);
        else if (k == K_ALT_LEFT) move_word_left(&e);
        else if (k == K_ALT_RIGHT) move_word_right(&e);
        else if (k == K_ALT_BACKSPACE) ed_kill_word_back(&e);
        else if (k == K_ALT_D) ed_kill_word_fwd(&e);
        else if (k == K_ALT_Y) yank_older(&e);
        else if (k == K_IGNORE) { /* unknown escape sequence */ }
        else if (k == 0x1b) { /* bare escape: ignore */ }
        else if (k == '\x7f' || k == '\b') ed_del_back(&e);
        else if (k >= 0x20 && k < 0x7f) ed_insert(&e, k);
        /* other control bytes: ignore */
    }
    ed_free(&e);
    return NULL;
}
