#ifndef CSX_PARSER_H
#define CSX_PARSER_H

#include "shell.h"

#define CSX_MAX_WORDS 512
#define CSX_MAX_REDIR 32
#define CSX_MAX_CMDS  64

typedef enum {
    CSX_REDIR_IN = 0,   /* <  */
    CSX_REDIR_OUT,      /* >  */
    CSX_REDIR_APPEND    /* >> */
} csx_redir_mode;

typedef struct {
    int fd;             /* 0, 1, 2 */
    csx_redir_mode mode;
    int is_dup;         /* target is another fd (e.g. 2>&1) */
    int dup_fd;
    char *target;       /* filename (NULL if dup) */
} csx_redir;

typedef struct {
    char *words[CSX_MAX_WORDS];
    int nwords;
    csx_redir redirs[CSX_MAX_REDIR];
    int nredirs;
} csx_cmd;

typedef enum {
    CSX_OP_END = 0,
    CSX_OP_SEP,   /* ; */
    CSX_OP_AND,   /* && */
    CSX_OP_OR     /* || */
} csx_op;

typedef struct csx_node {
    csx_cmd cmds[CSX_MAX_CMDS];
    int ncmds;
    int background;     /* pipeline ended with & */
    csx_op op;          /* operator before the next node */
    struct csx_node *next;
} csx_node;

csx_node *csx_parse(const char *line);
void csx_free_node(csx_node *node);

#endif /* CSX_PARSER_H */
