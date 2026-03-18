#ifndef PARSER_H
#define PARSER_H

#include "vshell.h"

Token    *tokenize(const char *line, int *count);
void      free_tokens(Token *tokens, int count);
Pipeline *parse(Token *tokens, int token_count);
void      free_pipeline(Pipeline *pl);

#endif /* PARSER_H */
