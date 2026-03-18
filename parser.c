#include "parser.h"

/* ── Dynamic character buffer for building tokens ────────────────────── */

typedef struct {
    char *data;
    int   len;
    int   cap;
} Buffer;

static void buf_init(Buffer *b)
{
    b->cap  = 64;
    b->data = malloc(b->cap);
    b->len  = 0;
}

static void buf_push(Buffer *b, char c)
{
    if (b->len + 1 >= b->cap) {
        b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
    b->data[b->len++] = c;
}

static void buf_free(Buffer *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* ── Token array helpers ─────────────────────────────────────────────── */

static Token *tokens_add(Token *tokens, int *count, int *cap,
                         const char *value, int quoted)
{
    if (*count >= *cap) {
        *cap *= 2;
        tokens = realloc(tokens, sizeof(Token) * (*cap));
    }
    tokens[*count].value  = strdup(value);
    tokens[*count].quoted = quoted;
    (*count)++;
    return tokens;
}

static Token *flush_token(Token *tokens, Buffer *buf, int *in_token,
                          int *quoted, int *count, int *cap)
{
    if (*in_token) {
        buf_push(buf, '\0');
        tokens = tokens_add(tokens, count, cap, buf->data, *quoted);
        buf->len   = 0;          /* reuse buffer for next token */
        *in_token  = 0;
        *quoted    = 0;
    }
    return tokens;
}

/* ── Tokenizer ───────────────────────────────────────────────────────── */

Token *tokenize(const char *line, int *count)
{
    *count   = 0;
    int cap  = 32;
    Token *tokens = malloc(sizeof(Token) * cap);

    Buffer buf;
    buf_init(&buf);
    int in_token = 0;
    int quoted   = 0;

    const char *p = line;
    while (*p) {
        /* ── Single-quoted string ─────────────────────────────────── */
        if (*p == '\'') {
            in_token = 1;
            quoted   = 1;
            p++;
            while (*p && *p != '\'')
                buf_push(&buf, *p++);
            if (*p == '\'') p++;
            continue;
        }

        /* ── Double-quoted string ─────────────────────────────────── */
        if (*p == '"') {
            in_token = 1;
            quoted   = 1;
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1] &&
                    (p[1] == '"' || p[1] == '\\' || p[1] == '$')) {
                    p++;
                    buf_push(&buf, *p++);
                } else {
                    buf_push(&buf, *p++);
                }
            }
            if (*p == '"') p++;
            continue;
        }

        /* ── Backslash escape ─────────────────────────────────────── */
        if (*p == '\\' && p[1]) {
            in_token = 1;
            p++;
            buf_push(&buf, *p++);
            continue;
        }

        /* ── Whitespace → end current token ───────────────────────── */
        if (isspace((unsigned char)*p)) {
            tokens = flush_token(tokens, &buf, &in_token,
                                 &quoted, count, &cap);
            p++;
            continue;
        }

        /* ── Comment → ignore rest of line ────────────────────────── */
        if (*p == '#' && !in_token) {
            break;
        }

        /* ── Operators: |  >  >>  <  & ────────────────────────────── */
        if (*p == '|' || *p == '<' || *p == '>' || *p == '&') {
            tokens = flush_token(tokens, &buf, &in_token,
                                 &quoted, count, &cap);
            if (*p == '>' && p[1] == '>') {
                tokens = tokens_add(tokens, count, &cap, ">>", 0);
                p += 2;
            } else {
                char op[2] = {*p, '\0'};
                tokens = tokens_add(tokens, count, &cap, op, 0);
                p++;
            }
            continue;
        }

        /* ── Regular character ────────────────────────────────────── */
        in_token = 1;
        buf_push(&buf, *p++);
    }

    /* Flush trailing token */
    tokens = flush_token(tokens, &buf, &in_token, &quoted, count, &cap);
    buf_free(&buf);
    return tokens;
}

void free_tokens(Token *tokens, int count)
{
    if (!tokens) return;
    for (int i = 0; i < count; i++)
        free(tokens[i].value);
    free(tokens);
}

/* ── Parser: tokens → Pipeline ───────────────────────────────────────── */

static void cmd_init(Command *cmd)
{
    cmd->args       = calloc(VSHELL_MAX_ARGS, sizeof(char *));
    cmd->arg_quoted = calloc(VSHELL_MAX_ARGS, sizeof(int));
    cmd->argc       = 0;
    cmd->infile     = NULL;
    cmd->outfile    = NULL;
    cmd->append     = 0;
}

Pipeline *parse(Token *tokens, int token_count)
{
    if (token_count == 0) return NULL;

    Pipeline *pl = malloc(sizeof(Pipeline));
    pl->commands   = calloc(VSHELL_MAX_CMDS, sizeof(Command));
    pl->count      = 0;
    pl->background = 0;

    /* Trailing & → background */
    if (token_count > 0 &&
        strcmp(tokens[token_count - 1].value, "&") == 0) {
        pl->background = 1;
        token_count--;
    }

    int ci = 0;                     /* current command index */
    cmd_init(&pl->commands[ci]);

    for (int i = 0; i < token_count; i++) {
        const char *tok = tokens[i].value;

        /* ── Pipe ─────────────────────────────────────────────────── */
        if (strcmp(tok, "|") == 0) {
            if (pl->commands[ci].argc == 0) {
                fprintf(stderr, "vshell: syntax error near '|'\n");
                free_pipeline(pl);
                return NULL;
            }
            ci++;
            if (ci >= VSHELL_MAX_CMDS) {
                fprintf(stderr, "vshell: too many commands in pipeline\n");
                free_pipeline(pl);
                return NULL;
            }
            cmd_init(&pl->commands[ci]);
            continue;
        }

        /* ── Input redirection ────────────────────────────────────── */
        if (strcmp(tok, "<") == 0) {
            if (i + 1 >= token_count) {
                fprintf(stderr, "vshell: syntax error near '<'\n");
                free_pipeline(pl);
                return NULL;
            }
            free(pl->commands[ci].infile);
            pl->commands[ci].infile = strdup(tokens[++i].value);
            continue;
        }

        /* ── Output redirection (append) ──────────────────────────── */
        if (strcmp(tok, ">>") == 0) {
            if (i + 1 >= token_count) {
                fprintf(stderr, "vshell: syntax error near '>>'\n");
                free_pipeline(pl);
                return NULL;
            }
            free(pl->commands[ci].outfile);
            pl->commands[ci].outfile = strdup(tokens[++i].value);
            pl->commands[ci].append  = 1;
            continue;
        }

        /* ── Output redirection ───────────────────────────────────── */
        if (strcmp(tok, ">") == 0) {
            if (i + 1 >= token_count) {
                fprintf(stderr, "vshell: syntax error near '>'\n");
                free_pipeline(pl);
                return NULL;
            }
            free(pl->commands[ci].outfile);
            pl->commands[ci].outfile = strdup(tokens[++i].value);
            pl->commands[ci].append  = 0;
            continue;
        }

        /* ── Regular argument ─────────────────────────────────────── */
        Command *cmd = &pl->commands[ci];
        cmd->args[cmd->argc]       = strdup(tokens[i].value);
        cmd->arg_quoted[cmd->argc] = tokens[i].quoted;
        cmd->argc++;
    }

    pl->count = ci + 1;

    /* Validate: no empty commands */
    for (int i = 0; i < pl->count; i++) {
        if (pl->commands[i].argc == 0) {
            fprintf(stderr, "vshell: syntax error: empty command\n");
            free_pipeline(pl);
            return NULL;
        }
    }
    return pl;
}

void free_pipeline(Pipeline *pl)
{
    if (!pl) return;
    if (pl->commands) {
        for (int i = 0; i < VSHELL_MAX_CMDS; i++) {
            Command *cmd = &pl->commands[i];
            if (cmd->args) {
                for (int j = 0; cmd->args[j]; j++)
                    free(cmd->args[j]);
                free(cmd->args);
            }
            free(cmd->arg_quoted);
            free(cmd->infile);
            free(cmd->outfile);
        }
        free(pl->commands);
    }
    free(pl);
}
