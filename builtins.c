#include "builtins.h"

/* ── Individual built-in implementations ─────────────────────────────── */

static int builtin_cd(Command *cmd)
{
    const char *dir;
    if (cmd->argc < 2) {
        dir = getenv("HOME");
        if (!dir) {
            fprintf(stderr, "cd: HOME not set\n");
            return 1;
        }
    } else {
        dir = cmd->args[1];
    }

    if (chdir(dir) != 0) {
        fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
        return 1;
    }

    /* Keep $PWD in sync */
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)))
        setenv("PWD", cwd, 1);
    return 0;
}

static int builtin_exit(Command *cmd)
{
    int code = 0;
    if (cmd->argc > 1)
        code = atoi(cmd->args[1]);
    exit(code);
    return 0;   /* unreachable */
}

static int builtin_pwd(Command *cmd)
{
    (void)cmd;
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
        return 0;
    }
    perror("pwd");
    return 1;
}

static int builtin_export(Command *cmd)
{
    if (cmd->argc < 2) {
        extern char **environ;
        for (char **ep = environ; *ep; ep++)
            printf("export %s\n", *ep);
        return 0;
    }

    for (int i = 1; i < cmd->argc; i++) {
        char *eq = strchr(cmd->args[i], '=');
        if (eq) {
            *eq = '\0';
            setenv(cmd->args[i], eq + 1, 1);
            *eq = '=';
        } else {
            /* export VAR without value */
            if (!getenv(cmd->args[i]))
                setenv(cmd->args[i], "", 1);
        }
    }
    return 0;
}

static int builtin_unset(Command *cmd)
{
    for (int i = 1; i < cmd->argc; i++)
        unsetenv(cmd->args[i]);
    return 0;
}

static int builtin_env(Command *cmd)
{
    (void)cmd;
    extern char **environ;
    for (char **ep = environ; *ep; ep++)
        printf("%s\n", *ep);
    return 0;
}

static int builtin_help(Command *cmd)
{
    (void)cmd;
    printf("vshell - a mini shell\n\n");
    printf("Built-in commands:\n");
    printf("  cd [dir]          Change directory (default: $HOME)\n");
    printf("  exit [code]       Exit the shell\n");
    printf("  pwd               Print working directory\n");
    printf("  export VAR=val    Set environment variable\n");
    printf("  unset VAR         Unset environment variable\n");
    printf("  env               Print all environment variables\n");
    printf("  help              Show this help message\n");
    printf("\nFeatures:\n");
    printf("  cmd1 | cmd2       Pipeline\n");
    printf("  cmd > file        Output redirection\n");
    printf("  cmd >> file       Append redirection\n");
    printf("  cmd < file        Input redirection\n");
    printf("  cmd &             Background execution\n");
    printf("  $VAR ${VAR}       Variable expansion\n");
    printf("  $? $$             Last status / shell PID\n");
    printf("  * ? [...]         Glob expansion\n");
    printf("  Up/Down arrows    Command history\n");
    return 0;
}

/* ── Dispatch table ──────────────────────────────────────────────────── */

typedef int (*builtin_fn)(Command *);

static struct {
    const char *name;
    builtin_fn  fn;
} builtins_table[] = {
    { "cd",     builtin_cd     },
    { "exit",   builtin_exit   },
    { "pwd",    builtin_pwd    },
    { "export", builtin_export },
    { "unset",  builtin_unset  },
    { "env",    builtin_env    },
    { "help",   builtin_help   },
    { NULL,     NULL           },
};

int is_builtin(const char *cmd)
{
    for (int i = 0; builtins_table[i].name; i++)
        if (strcmp(cmd, builtins_table[i].name) == 0)
            return 1;
    return 0;
}

int exec_builtin(Command *cmd)
{
    for (int i = 0; builtins_table[i].name; i++)
        if (strcmp(cmd->args[0], builtins_table[i].name) == 0)
            return builtins_table[i].fn(cmd);
    return 1;
}
