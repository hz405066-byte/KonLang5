#ifndef KON_PARSER_H
#define KON_PARSER_H

#include <stddef.h>
#include "ast.h"

/* Разбор исходника. err — human-readable сообщение с номером строки. */
int parse_program(const char *src, size_t len, Program **out,
                  char *err, size_t errsz);

#endif
