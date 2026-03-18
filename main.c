#include "vshell.h"
#include "parser.h"
#include "executor.h"
#include "expand.h"

#include <readline/readline.h>
#include <readline/history.h>

/* ── Signal handlers ─────────────────────────────────────────────────── */

static void sigint_handler(int sig)
{
    (void)sig;
    write(STDOUT_FILENO, "\n", 1);
    rl_on_new_line();
    rl_replace_line("", 0);
    rl_redisplay();
}

/* ── Prompt ──────────────────────────────────────────────────────────── */

static char *build_prompt(void)
{
    static char prompt[1024];
    char cwd[512];

    char *user = getenv("USER");
    if (!user) user = "user";

    if (getcwd(cwd, sizeof(cwd)) == NULL)
        strcpy(cwd, "?");

    /* Shorten $HOME prefix to ~ */
    char display[512];
    char *home = getenv("HOME");
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        snprintf(display, sizeof(display), "~%s", cwd + strlen(home));
    } else {
        strncpy(display, cwd, sizeof(display) - 1);
        display[sizeof(display) - 1] = '\0';
    }

    /*
     * \001 / \002 are RL_PROMPT_START_IGNORE / RL_PROMPT_END_IGNORE
     * so readline doesn't count ANSI escapes in prompt width.
     */
    snprintf(prompt, sizeof(prompt),
             "\001\033[1;32m\002%s\001\033[0m\002"
             ":"
             "\001\033[1;34m\002%s\001\033[0m\002$ ",
             user, display);
    return prompt;
}

/* ── History file ────────────────────────────────────────────────────── */

static char *history_path(void)
{
    static char path[512];
    char *home = getenv("HOME");
    if (home)
        snprintf(path, sizeof(path), "%s/.vshell_history", home);
    else
        strcpy(path, ".vshell_history");
    return path;
}

/* ── Reap finished background processes ──────────────────────────────── */

static void reap_background(void)
{
    int   status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status))
            printf("[done] %d (exit %d)\n", pid, WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            printf("[done] %d (signal %d)\n", pid, WTERMSIG(status));
    }
}

/* ── Main loop ───────────────────────────────────────────────────────── */

int main(void)
{
    /* ── Signal setup ─────────────────────────────────────────────── */
    struct sigaction sa = {0};
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    /* ── History ──────────────────────────────────────────────────── */
    using_history();
    read_history(history_path());

    printf("vshell - a mini shell (type 'help' for commands)\n");

    char *line;
    while (1) {
        reap_background();

        line = readline(build_prompt());
        if (!line) break;                /* EOF (Ctrl-D) */
        if (*line == '\0') {
            free(line);
            continue;
        }

        add_history(line);

        /* 1. Expand environment variables */
        char *expanded = expand_variables(line);
        free(line);
        if (!expanded) continue;

        /* 2. Tokenize */
        int token_count;
        Token *tokens = tokenize(expanded, &token_count);
        free(expanded);
        if (!tokens || token_count == 0) {
            free_tokens(tokens, token_count);
            continue;
        }

        /* 3. Parse into pipeline */
        Pipeline *pl = parse(tokens, token_count);
        free_tokens(tokens, token_count);
        if (!pl) continue;

        /* 4. Execute */
        execute_pipeline(pl);
        free_pipeline(pl);
    }

    /* Save history on exit */
    write_history(history_path());
    printf("\n");
    return 0;
}
