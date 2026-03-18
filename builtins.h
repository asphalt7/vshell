#ifndef BUILTINS_H
#define BUILTINS_H

#include "vshell.h"

int is_builtin(const char *cmd);
int exec_builtin(Command *cmd);

#endif /* BUILTINS_H */
