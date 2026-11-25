#ifndef KON_INTERP_H
#define KON_INTERP_H

#include <stddef.h>
#include "ast.h"

/* Выполнить AST без генерации .COM/.EXE. */
int kon_run(Program *p, char *err, size_t errsz);

#endif
