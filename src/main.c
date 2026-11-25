/*
 * main.c -- CLI: check / run / build.
 * На шаге 1 рабочий только check; два других режима объявлены, но пока
 * сообщают о сроке появления, чтобы конвейер был виден целиком.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cp866.h"
#include "parser.h"
#include "ast.h"
#include "codegen.h"
#include "interp.h"

static void usage(void)
{
    fputs("KonLang v5 compiler (step 1)\n"
          "  konlang check <file.kn>            только разбор + AST\n"
          "  konlang run   <file.kn>            интерпретация (шаг 3)\n"
          "  konlang build <file.kn> [--exe]    сборка .COM/.EXE (при overflow -- .EXE)\n",
          stderr);
}

int main(int argc, char **argv)
{
    char *src, *path;
    size_t len = 0;
    char err[512];
    Program *prog = NULL;
    const char *mode;

    if (argc < 3) { usage(); return 2; }
    mode = argv[1];
    path = argv[2];

    if (strcmp(mode, "check") != 0 && strcmp(mode, "run") != 0 &&
        strcmp(mode, "build") != 0) {
        usage(); return 2;
    }

    src = kon_read_text(path, &len, err, sizeof(err));
    if (!src) { fprintf(stderr, "%s: %s\n", path, err); return 1; }
    printf("; %s: %lu байт, кодировка %s\n",
           path, (unsigned long)len, kon_source_encoding());

    if (!parse_program(src, len, &prog, err, sizeof(err))) {
        fprintf(stderr, "%s:%s\n", path, err);
        free(src);
        return 1;
    }
    printf("; разбор OK, команд верхнего уровня: %d\n", prog->body->n);

    if (strcmp(mode, "check") == 0) {
        ast_dump(stdout, prog);
    } else if (strcmp(mode, "run") == 0) {
        if (!kon_run(prog, err, sizeof(err))) {
            fprintf(stderr, "%s: %s\n", path, err);
            ast_free_program(prog);
            free(src);
            return 1;
        }
    } else {
        char out[256];
        int exe = 0, i;
        for (i = 3; i < argc; i++)
            if (strcmp(argv[i], "--exe") == 0) exe = 1;
        kon_output_name(path, exe, out, sizeof(out));
        if (exe ? !kon_build_exe(prog, out, err, sizeof(err))
                : !kon_build_com(prog, out, err, sizeof(err))) {
            if (!exe && strstr(err, "не влезает") != NULL) {
                fprintf(stderr, "; warning: %s; повторяю сборку как .EXE\n", err);
                kon_output_name(path, 1, out, sizeof(out));
                if (!kon_build_exe(prog, out, err, sizeof(err))) {
                    fprintf(stderr, "%s: %s\n", path, err);
                    ast_free_program(prog);
                    free(src);
                    return 1;
                }
            } else {
                fprintf(stderr, "%s: %s\n", path, err);
                ast_free_program(prog);
                free(src);
                return 1;
            }
        }
    }

    ast_free_program(prog);
    free(src);
    return 0;
}
