#include "executor.h"
#include "builtins.h"
#include "expand.h"

int vshell_last_status = 0;

/* ── Set up I/O redirections in child process ────────────────────────── */

static int setup_redirections(Command *cmd)
{
    if (cmd->infile) {
        int fd = open(cmd->infile, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "vshell: %s: %s\n", cmd->infile, strerror(errno));
            return -1;
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (cmd->outfile) {
        int flags = O_WRONLY | O_CREAT;
        flags |= cmd->append ? O_APPEND : O_TRUNC;
        int fd = open(cmd->outfile, flags, 0644);
        if (fd < 0) {
            fprintf(stderr, "vshell: %s: %s\n", cmd->outfile, strerror(errno));
            return -1;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    return 0;
}

/* ── Execute a pipeline ──────────────────────────────────────────────── */

int execute_pipeline(Pipeline *pl)
{
    if (pl->count == 0) return 0;

    /* Expand tilde & globs for every command */
    for (int i = 0; i < pl->count; i++)
        expand_globs(&pl->commands[i]);

    /* ── Single built-in (no pipe, foreground) → run in parent ────── */
    if (pl->count == 1 && !pl->background &&
        is_builtin(pl->commands[0].args[0])) {

        Command *cmd = &pl->commands[0];
        int saved_in  = -1;
        int saved_out = -1;

        /* Redirect if needed */
        if (cmd->infile) {
            saved_in = dup(STDIN_FILENO);
            int fd = open(cmd->infile, O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "vshell: %s: %s\n",
                        cmd->infile, strerror(errno));
                if (saved_in >= 0) close(saved_in);
                return (vshell_last_status = 1);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        if (cmd->outfile) {
            saved_out = dup(STDOUT_FILENO);
            int flags = O_WRONLY | O_CREAT |
                        (cmd->append ? O_APPEND : O_TRUNC);
            int fd = open(cmd->outfile, flags, 0644);
            if (fd < 0) {
                fprintf(stderr, "vshell: %s: %s\n",
                        cmd->outfile, strerror(errno));
                if (saved_in  >= 0) {
                    dup2(saved_in, STDIN_FILENO);
                    close(saved_in);
                }
                if (saved_out >= 0) close(saved_out);
                return (vshell_last_status = 1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        vshell_last_status = exec_builtin(cmd);

        /* Restore */
        if (saved_in  >= 0) { dup2(saved_in,  STDIN_FILENO);  close(saved_in);  }
        if (saved_out >= 0) { dup2(saved_out, STDOUT_FILENO); close(saved_out); }
        return vshell_last_status;
    }

    /* ── Create pipes ─────────────────────────────────────────────── */
    int pipefds[VSHELL_MAX_CMDS - 1][2];
    for (int i = 0; i < pl->count - 1; i++) {
        if (pipe(pipefds[i]) < 0) {
            perror("vshell: pipe");
            return (vshell_last_status = 1);
        }
    }

    /* ── Fork children ────────────────────────────────────────────── */
    pid_t pids[VSHELL_MAX_CMDS];

    for (int i = 0; i < pl->count; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("vshell: fork");
            return (vshell_last_status = 1);
        }

        if (pids[i] == 0) {
            /* ── Child ────────────────────────────────────────────── */

            /* Restore default signal disposition */
            signal(SIGINT,  SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            /* Wire up pipe input */
            if (i > 0) {
                dup2(pipefds[i - 1][0], STDIN_FILENO);
            }
            /* Wire up pipe output */
            if (i < pl->count - 1) {
                dup2(pipefds[i][1], STDOUT_FILENO);
            }

            /* Close all pipe fds */
            for (int j = 0; j < pl->count - 1; j++) {
                close(pipefds[j][0]);
                close(pipefds[j][1]);
            }

            /* Explicit redirections override pipes */
            if (setup_redirections(&pl->commands[i]) < 0)
                _exit(1);

            /* Built-in inside pipeline → run and exit */
            if (is_builtin(pl->commands[i].args[0]))
                _exit(exec_builtin(&pl->commands[i]));

            /* External command */
            execvp(pl->commands[i].args[0], pl->commands[i].args);
            fprintf(stderr, "vshell: %s: %s\n",
                    pl->commands[i].args[0], strerror(errno));
            _exit(127);
        }
    }

    /* ── Parent: close all pipe fds ───────────────────────────────── */
    for (int i = 0; i < pl->count - 1; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }

    /* ── Wait ─────────────────────────────────────────────────────── */
    if (!pl->background) {
        int status = 0;
        for (int i = 0; i < pl->count; i++)
            waitpid(pids[i], &status, 0);

        /* Use last command's exit status */
        if (WIFEXITED(status))
            vshell_last_status = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            vshell_last_status = 128 + WTERMSIG(status);
    } else {
        printf("[bg]");
        for (int i = 0; i < pl->count; i++)
            printf(" %d", pids[i]);
        printf("\n");
    }

    return vshell_last_status;
}
