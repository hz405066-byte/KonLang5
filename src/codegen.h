#ifndef KON_CODEGEN_H
#define KON_CODEGEN_H

#include <stddef.h>
#include "ast.h"

/* Имя выходного файла: VERH.kn -> VERH.COM (или .EXE). */
void kon_output_name(const char *srcpath, int exe, char *out, size_t outsz);

/* Генерация .COM (шаг 2). --exe пока доступен только на шаге 6. */
int kon_build_com(Program *p, const char *outpath, char *err, size_t errsz);
int kon_build_exe(Program *p, const char *outpath, char *err, size_t errsz);

#endif
