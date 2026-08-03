#include "parser.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*
 * Read one raw shell token.  Quotes and backslashes are preserved so the
 * expander (expand.c) can interpret them quote-aware; $VAR/$()/$? are NOT
 * expanded here, they are deferred to execution time (execute.c) so that
 * $? reflects the previous command and $(...) runs in the right order.
 * Stops at whitespace or a control operator.  Leaves *pp pointing at the
 * first unconsumed character.  Returns a malloc'd copy, or NULL if empty.
 */
static char *read_raw_word(const char **pp)
{
    const char *p = *pp;
    strbuf raw;
    int paren = 0;

    sb_init(&raw);
    while (*p) {
        char c = *p;
        if (c == '\'' || c == '"') {
            char q = c;
            sb_putc(&raw, q);
            p++;
            while (*p && *p != q) {
                if (q == '"' && *p == '\\' &&
                    (p[1] == '"' || p[1] == '\\' || p[1] == '$')) {
                    sb_putc(&raw, '\\');
                    sb_putc(&raw, p[1]);
                    p += 2;
                    continue;
                }
                sb_putc(&raw, *p++);
            }
            if (*p == q) {
                sb_putc(&raw, q);
                p++;
            }
            continue;
        }
        if (c == '\\') {
            sb_putc(&raw, c);
            if (p[1])
                sb_putc(&raw, p[1]);
            p += 2;
            continue;
        }
        /* track $( ... ) and ${ ... } so inner spaces/operators are kept */
        if (c == '$' && (p[1] == '(' || p[1] == '{')) {
            sb_putc(&raw, c);
            sb_putc(&raw, p[1]);
            paren++;
            p += 2;
            continue;
        }
        if (paren > 0) {
            if (c == '(' || c == '{')
                paren++;
            else if (c == ')' || c == '}')
                paren--;
            sb_putc(&raw, c);
            p++;
            continue;
        }
        if (isspace(c) || c == ';' || c == '|' || c == '&' || c == '>' || c == '<')
            break;
        sb_putc(&raw, *p++);
    }
    *pp = p;
    if (raw.len == 0) {
        sb_free(&raw);
        return NULL;
    }
    return sb_detach(&raw);
}

static csx_cmd *ensure_cmd(csx_node **cur)
{
    if (!*cur)
        *cur = calloc(1, sizeof(csx_node));
    return &(*cur)->cmds[(*cur)->ncmds];
}

static void finish_cmd(csx_node *cur)
{
    if (cur) {
        csx_cmd *c = &cur->cmds[cur->ncmds];
        if (c->nwords > 0)
            c->words[c->nwords] = NULL;
        cur->ncmds++;
    }
}

static void finish_node(csx_node **head, csx_node **tail, csx_node **cur, csx_op op)
{
    if (!*cur)
        return;
    if ((*cur)->ncmds == 0 && (*cur)->background == 0) {
        free(*cur);
        *cur = NULL;
        return;
    }
    (*cur)->op = op;
    if (!*head)
        *head = *cur;
    else
        (*tail)->next = *cur;
    *tail = *cur;
    *cur = NULL;
}

csx_node *csx_parse(const char *line)
{
    csx_node *head = NULL, *tail = NULL, *cur = NULL;
    const char *p = line;

    for (;;) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        if (*p == '#')
            break; /* comment to end of line */

        /* --- redirections: > < >> 2> 2>> 2>&1 >&N <&N --- */
        int fd = -1;
        csx_redir_mode mode = CSX_REDIR_OUT;
        if (*p == '>' || *p == '<') {
            fd = (*p == '>') ? 1 : 0;
            mode = (*p == '>') ? CSX_REDIR_OUT : CSX_REDIR_IN;
            p++;
        } else if (isdigit(*p) && (p[1] == '>' || p[1] == '<')) {
            fd = *p - '0';
            mode = (p[1] == '>') ? CSX_REDIR_OUT : CSX_REDIR_IN;
            p += 2;
        }
        if (fd >= 0) {
            csx_cmd *c = ensure_cmd(&cur);
            csx_redir *r = &c->redirs[c->nredirs];
            r->fd = fd;
            r->mode = mode;
            r->is_dup = 0;
            r->target = NULL;
            if (mode == CSX_REDIR_OUT && *p == '>') {
                r->mode = CSX_REDIR_APPEND;
                p++;
            }
            if (*p == '&') {
                p++;
                r->is_dup = 1;
                if (isdigit(*p)) {
                    r->dup_fd = *p - '0';
                    p++;
                } else {
                    r->dup_fd = fd;
                }
            } else {
                while (*p == ' ' || *p == '\t')
                    p++;
                if (*p)
                    r->target = read_raw_word(&p);
            }
            c->nredirs++;
            continue;
        }

        /* --- operators --- */
        if (*p == ';') {
            finish_cmd(cur);
            finish_node(&head, &tail, &cur, CSX_OP_SEP);
            p++;
            continue;
        }
        if (*p == '&') {
            if (p[1] == '&') {
                finish_cmd(cur);
                finish_node(&head, &tail, &cur, CSX_OP_AND);
                p += 2;
                continue;
            }
            if (cur)
                cur->background = 1;
            finish_cmd(cur);
            finish_node(&head, &tail, &cur, CSX_OP_SEP);
            p++;
            continue;
        }
        if (*p == '|') {
            if (p[1] == '|') {
                finish_cmd(cur);
                finish_node(&head, &tail, &cur, CSX_OP_OR);
                p += 2;
                continue;
            }
            finish_cmd(cur);
            p++;
            continue;
        }

        /* --- word --- */
        {
            char *tok = read_raw_word(&p);
            if (tok) {
                csx_cmd *c = ensure_cmd(&cur);
                if (c->nwords < CSX_MAX_WORDS)
                    c->words[c->nwords++] = tok;
                else
                    free(tok);
                c->words[c->nwords] = NULL;
            }
        }
    }

    finish_cmd(cur);
    finish_node(&head, &tail, &cur, CSX_OP_END);
    return head;
}

void csx_free_node(csx_node *node)
{
    while (node) {
        csx_node *nx = node->next;
        int i, j;
        for (i = 0; i < node->ncmds; i++) {
            csx_cmd *c = &node->cmds[i];
            for (j = 0; j < c->nwords; j++)
                free(c->words[j]);
            for (j = 0; j < c->nredirs; j++)
                free(c->redirs[j].target);
        }
        free(node);
        node = nx;
    }
}
