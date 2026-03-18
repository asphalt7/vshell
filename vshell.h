#ifndef VSHELL_H
#define VSHELL_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <glob.h>
#include <ctype.h>

#define VSHELL_MAX_ARGS  256
#define VSHELL_MAX_CMDS  32

/* Token produced by the lexer */
typedef struct {
    char  *value;
    int    quoted;   /* 1 if token was (partially) inside quotes */
} Token;

/* A single command in a pipeline */
typedef struct {
    char **args;         /* NULL-terminated argument array */
    int   *arg_quoted;   /* parallel array: 1 if the arg was quoted */
    int    argc;
    char  *infile;       /* < redirection */
    char  *outfile;      /* > or >> redirection */
    int    append;       /* 1 for >>, 0 for > */
} Command;

/* A pipeline of commands connected by | */
typedef struct {
    Command *commands;
    int      count;
    int      background; /* 1 if ends with & */
} Pipeline;

/* Last exit status, set by the executor */
extern int vshell_last_status;

#endif /* VSHELL_H */
