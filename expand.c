#include "expand.h"

/* ── Environment variable expansion ──────────────────────────────────── */

char *expand_variables(const char *line)
{
    int cap = (int)strlen(line) * 2 + 64;
    char *result = malloc(cap);
    int len = 0;

    int in_single_quote = 0;
    int in_double_quote = 0;

    const char *p = line;
    while (*p) {
        /* Ensure space */
        if (len + 512 >= cap) {
            cap *= 2;
            result = realloc(result, cap);
        }

        /* ── Quote tracking ───────────────────────────────────────── */
        if (*p == '\'' && !in_double_quote) {
            result[len++] = *p++;
            in_single_quote = !in_single_quote;
            continue;
        }
        if (*p == '"' && !in_single_quote) {
            result[len++] = *p++;
            in_double_quote = !in_double_quote;
            continue;
        }

        /* No expansion inside single quotes */
        if (in_single_quote) {
            result[len++] = *p++;
            continue;
        }

        /* ── $ expansion (outside single quotes) ──────────────────── */
        if (*p == '$') {
            p++;

            /* $$ → current PID */
            if (*p == '$') {
                p++;
                len += snprintf(result + len, cap - len, "%d", (int)getpid());
                continue;
            }

            /* $? → last exit status */
            if (*p == '?') {
                p++;
                len += snprintf(result + len, cap - len, "%d",
                                vshell_last_status);
                continue;
            }

            /* ${VAR} */
            if (*p == '{') {
                p++;
                const char *start = p;
                while (*p && *p != '}') p++;
                int vlen = (int)(p - start);
                char varname[256];
                if (vlen > 0 && vlen < (int)sizeof(varname)) {
                    memcpy(varname, start, vlen);
                    varname[vlen] = '\0';
                    const char *val = getenv(varname);
                    if (val) {
                        int slen = (int)strlen(val);
                        while (len + slen >= cap) {
                            cap *= 2;
                            result = realloc(result, cap);
                        }
                        memcpy(result + len, val, slen);
                        len += slen;
                    }
                }
                if (*p == '}') p++;
                continue;
            }

            /* $VAR */
            if (isalpha((unsigned char)*p) || *p == '_') {
                const char *start = p;
                while (isalnum((unsigned char)*p) || *p == '_') p++;
                int vlen = (int)(p - start);
                char varname[256];
                if (vlen < (int)sizeof(varname)) {
                    memcpy(varname, start, vlen);
                    varname[vlen] = '\0';
                    const char *val = getenv(varname);
                    if (val) {
                        int slen = (int)strlen(val);
                        while (len + slen >= cap) {
                            cap *= 2;
                            result = realloc(result, cap);
                        }
                        memcpy(result + len, val, slen);
                        len += slen;
                    }
                }
                continue;
            }

            /* Lone $ → keep literal */
            result[len++] = '$';
            continue;
        }

        /* Regular character */
        result[len++] = *p++;
    }

    result[len] = '\0';
    return result;
}

/* ── Tilde & glob expansion ──────────────────────────────────────────── */

void expand_globs(Command *cmd)
{
    char **new_args = calloc(VSHELL_MAX_ARGS, sizeof(char *));
    int new_argc = 0;

    for (int i = 0; i < cmd->argc && new_argc < VSHELL_MAX_ARGS - 1; i++) {
        /* Skip expansion for quoted args */
        if (cmd->arg_quoted && cmd->arg_quoted[i]) {
            new_args[new_argc++] = cmd->args[i];
            cmd->args[i] = NULL;
            continue;
        }

        /* ── Tilde expansion ──────────────────────────────────────── */
        if (cmd->args[i][0] == '~') {
            char *home = getenv("HOME");
            if (home) {
                if (cmd->args[i][1] == '\0') {
                    free(cmd->args[i]);
                    cmd->args[i] = strdup(home);
                } else if (cmd->args[i][1] == '/') {
                    int needed = (int)strlen(home) + (int)strlen(cmd->args[i]);
                    char *expanded = malloc(needed + 1);
                    snprintf(expanded, needed + 1, "%s%s",
                             home, cmd->args[i] + 1);
                    free(cmd->args[i]);
                    cmd->args[i] = expanded;
                }
            }
        }

        /* ── Glob expansion ───────────────────────────────────────── */
        if (strpbrk(cmd->args[i], "*?[")) {
            glob_t g;
            if (glob(cmd->args[i], GLOB_NOCHECK, NULL, &g) == 0) {
                for (size_t j = 0;
                     j < g.gl_pathc && new_argc < VSHELL_MAX_ARGS - 1; j++) {
                    new_args[new_argc++] = strdup(g.gl_pathv[j]);
                }
                free(cmd->args[i]);
                globfree(&g);
            } else {
                new_args[new_argc++] = cmd->args[i];
            }
        } else {
            new_args[new_argc++] = cmd->args[i];
        }
        cmd->args[i] = NULL;
    }

    new_args[new_argc] = NULL;   /* keep NULL-terminated */

    free(cmd->args);
    cmd->args = new_args;
    cmd->argc = new_argc;
}
