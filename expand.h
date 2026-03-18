#ifndef EXPAND_H
#define EXPAND_H

#include "vshell.h"

char *expand_variables(const char *line);
void  expand_globs(Command *cmd);

#endif /* EXPAND_H */
