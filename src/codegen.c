/*
 * codegen.c -- генератор .COM.
 *
 * Схема: ORG 100h, один сегмент, код -> runtime -> данные. Всё в одном
 * ассемблерном файле, который собирается `nasm -f bin`. Символы "
 *.forward reference" NASM разрешает, поэтому данные можно печатать после кода.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include "codegen.h"
#include "cp866.h"
#include "ast.h"

#ifndef KON_RUNTIME_INC
#define KON_RUNTIME_INC "runtime/rt_console.inc"
#endif

#define COM_LIMIT 65280          /* 65536 - 256 (ORG 100h) */

typedef struct { char *name; char *label; } Var;
typedef struct { char *label; char *utf8; unsigned char *bytes; size_t n; } Lit;
typedef struct { char *var, *label; int n; Expr **items; } ListTab;
typedef struct { char *name; Block *body; } KonEnt;

typedef struct {
    FILE *f;
    Var *vars; int nvars;
    Lit *lits; int nlits;
    int uid;
    int loop_end[32];
    int nloop;
    ListTab lists[32];
    int nlists;
    KonEnt kons[32];
    int nkons;
    char *err;
    size_t errsz;
} Cg;

static const char *list_label(Cg *cg, const char *var);

static void seterr(Cg *cg, const char *msg)
{
    if (cg->err && cg->errsz) sprintf(cg->err, "%s", msg);
}

void kon_output_name(const char *srcpath, int exe, char *out, size_t outsz)
{
    const char *b = srcpath, *p;
    size_t i = 0;
    for (p = srcpath; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    for (p = b; *p && *p != '.'; p++) {
        if (i + 1 < outsz) out[i++] = (char)toupper((unsigned char)*p);
    }
    if (i + 5 < outsz) {
        if (exe) { out[i++] = '.'; out[i++] = 'E'; out[i++] = 'X'; out[i++] = 'E'; }
        else     { out[i++] = '.'; out[i++] = 'C'; out[i++] = 'O'; out[i++] = 'M'; }
    }
    out[i] = 0;
}

static char *intern_var(Cg *cg, const char *name)
{
    int i;
    char buf[32];
    Var *v;
    for (i = 0; i < cg->nvars; i++)
        if (strcmp(cg->vars[i].name, name) == 0) return cg->vars[i].label;
    v = (Var *)realloc(cg->vars, (size_t)(cg->nvars+1)*sizeof(Var));
    if (!v) { seterr(cg, "нет памяти"); return NULL; }
    cg->vars = v;
    sprintf(buf, "v%d", cg->nvars);
    cg->vars[cg->nvars].name = (char *)malloc(strlen(name)+1);
    strcpy(cg->vars[cg->nvars].name, name);
    cg->vars[cg->nvars].label = (char *)malloc(strlen(buf)+1);
    strcpy(cg->vars[cg->nvars].label, buf);
    return cg->vars[cg->nvars++].label;
}

static char *intern_lit(Cg *cg, Expr *e)
{
    int i;
    char buf[32];
    Lit *l;
    char *cp;
    size_t n;
    for (i = 0; i < cg->nlits; i++) {
        if (strcmp(cg->lits[i].utf8, e->u.s) == 0)
            return cg->lits[i].label;
    }
    l = (Lit *)realloc(cg->lits, (size_t)(cg->nlits+1)*sizeof(Lit));
    if (!l) { seterr(cg, "нет памяти"); return NULL; }
    cg->lits = l;
    cp = kon_to_cp866(e->u.s);
    if (!cp) { seterr(cg, "нет памяти"); return NULL; }
    n = strlen(cp);
    sprintf(buf, "s%d", cg->nlits);
    cg->lits[cg->nlits].label = (char *)malloc(strlen(buf)+1);
    strcpy(cg->lits[cg->nlits].label, buf);
    cg->lits[cg->nlits].utf8 = (char *)malloc(strlen(e->u.s)+1);
    strcpy(cg->lits[cg->nlits].utf8, e->u.s);
    cg->lits[cg->nlits].bytes = (unsigned char *)malloc(n ? n : 1);
    memcpy(cg->lits[cg->nlits].bytes, cp, n);
    cg->lits[cg->nlits].n = n;
    free(cp);
    return cg->lits[cg->nlits++].label;
}

/* Вычисление выражения в AX (16-битная целая арифметика). */
static int emit_int_expr(Cg *cg, Expr *e)
{
    char *lab;
    if (!e) { seterr(cg, "пустое выражение"); return 0; }
    switch (e->kind) {
    case EX_INT:
        fprintf(cg->f, "        mov     ax, %ld\n", e->u.i);
        return 1;
    case EX_VAR:
        lab = intern_var(cg, e->u.var.name);
        if (!lab) return 0;
        fprintf(cg->f, "        mov     si, %s+2\n        call    str2int\n", lab);
        return 1;
    case EX_LEN:
        if (!e->u.inner || e->u.inner->kind != EX_VAR) {
            seterr(cg, "len: только переменные"); return 0;
        }
        lab = intern_var(cg, e->u.inner->u.var.name);
        if (!lab) return 0;
        {
            const char *tl = list_label(cg, e->u.inner->u.var.name);
            int k;
            for (k = 0; tl && k < cg->nlists; k++)
                if (strcmp(cg->lists[k].label, tl) == 0) break;
            if (tl && k < cg->nlists)
                fprintf(cg->f, "        mov     ax, %d\n", cg->lists[k].n);
            else
                fprintf(cg->f, "        mov     si, %s+2\n        call    strlen_ax\n", lab);
        }
        return 1;
    case EX_MATH:
        return emit_int_expr(cg, e->u.math.a);
    case EX_BIN:
        if (!emit_int_expr(cg, e->u.bin.a)) return 0;
        fprintf(cg->f, "        push    ax\n");
        if (!emit_int_expr(cg, e->u.bin.b)) return 0;
        fprintf(cg->f, "        mov     bx, ax\n        pop     ax\n");
        if (strcmp(e->u.bin.op, "+") == 0) fprintf(cg->f, "        add     ax, bx\n");
        else if (strcmp(e->u.bin.op, "-") == 0) fprintf(cg->f, "        sub     ax, bx\n");
        else if (strcmp(e->u.bin.op, "*") == 0) fprintf(cg->f, "        imul    bx\n");
        else if (strcmp(e->u.bin.op, "/") == 0)
            fprintf(cg->f, "        test    bx, bx\n"
                           "        jz      div0_error\n"
                           "        xor     dx, dx\n"
                           "        idiv    bx\n");
        else { seterr(cg, "неподдерживаемая арифметическая операция"); return 0; }
        return 1;
    default:
        seterr(cg, "выражение не является числом"); return 0;
    }
}

static int emit_math(Cg *cg, Expr *e)
{
    Expr *a = e->u.math.a;
    const char *t = e->u.math.type;
    if (t && (strcmp(t, "float") == 0 || strcmp(t, "double") == 0)) {
        if (!a || a->kind != EX_BIN || strcmp(a->u.bin.op, "/") != 0) {
            seterr(cg, "float в .COM: пока только деление двух чисел "
                       "(остальное -- через `run`)");
            return 0;
        }
        if (!emit_int_expr(cg, a->u.bin.a)) return 0;
        fprintf(cg->f, "        push    ax\n");
        if (!emit_int_expr(cg, a->u.bin.b)) return 0;
        fprintf(cg->f, "        mov     bx, ax\n        pop     ax\n"
                       "        call    append_fdiv\n");
        return 1;
    }
    if (!emit_int_expr(cg, a)) return 0;
    fprintf(cg->f, "        call    append_int\n");
    return 1;
}

static int emit_expr(Cg *cg, Expr *e);
static int emit_block(Cg *cg, Block *b);
static const char *list_label(Cg *cg, const char *var);

static int emit_repeat(Cg *cg, Expr *e)
{
    int id = cg->uid++;
    if (!e->u.rep.count || e->u.rep.count->kind != EX_INT) {
        seterr(cg, "повтор строки: счётчик должен быть числом");
        return 0;
    }
    fprintf(cg->f, "        mov     cx, %ld\n", e->u.rep.count->u.i);
    fprintf(cg->f, ".rep%d:\n", id);
    if (!emit_expr(cg, e->u.rep.text)) return 0;
    fprintf(cg->f, "        dec     cx\n        jnz     near .rep%d\n", id);
    return 1;
}

static const char *intern_list(Cg *cg, const char *var, int n, Expr **items)
{
    int i;
    char buf[32];
    for (i = 0; i < cg->nlists; i++)
        if (strcmp(cg->lists[i].var, var) == 0) return cg->lists[i].label;
    if (cg->nlists >= 32) { seterr(cg, "слишком много списков"); return NULL; }
    sprintf(buf, "lst%d", cg->nlists);
    cg->lists[cg->nlists].var = (char *)malloc(strlen(var)+1);
    strcpy(cg->lists[cg->nlists].var, var);
    cg->lists[cg->nlists].label = (char *)malloc(strlen(buf)+1);
    strcpy(cg->lists[cg->nlists].label, buf);
    cg->lists[cg->nlists].n = n;
    cg->lists[cg->nlists].items = items;
    return cg->lists[cg->nlists++].label;
}

static const char *list_label(Cg *cg, const char *var)
{
    int i;
    for (i = 0; i < cg->nlists; i++)
        if (strcmp(cg->lists[i].var, var) == 0) return cg->lists[i].label;
    return NULL;
}

static int emit_random(Cg *cg, Stmt *s)
{
    char *lab = intern_var(cg, s->u.rnd.name);
    const char *tbl;
    int i;
    if (!lab || s->u.rnd.n == 0) return 0;
    for (i = 0; i < s->u.rnd.n; i++)
        if (s->u.rnd.items[i]->kind != EX_STR) {
            seterr(cg, "random: пока только строковые элементы"); return 0;
        }
    tbl = intern_list(cg, s->u.rnd.name, s->u.rnd.n, s->u.rnd.items);
    if (!tbl) return 0;
    fprintf(cg->f, "        mov     cx, %d\n"
                   "        call    rand_index\n"
                   "        mov     bx, ax\n"
                   "        shl     bx, 1\n"
                   "        mov     si, %s\n"
                   "        add     si, bx\n"
                   "        lodsw\n"
                   "        mov     si, ax\n"
                   "        mov     di, %s+2\n"
                   "        call    strcpy_ds\n",
            s->u.rnd.n, tbl, lab);
    return 1;
}

static int emit_expr(Cg *cg, Expr *e)
{
    char *lab;
    int i;
    if (!e) return 1;
    switch (e->kind) {
    case EX_STR:
        lab = intern_lit(cg, e);
        if (!lab) return 0;
        fprintf(cg->f, "        mov     si, %s\n        call    append_str\n", lab);
        return 1;
    case EX_INT:
        fprintf(cg->f, "        mov     ax, %ld\n        call    append_int\n", e->u.i);
        return 1;
    case EX_MATH:
        return emit_math(cg, e);
    case EX_CONCAT:
        for (i = 0; i < e->u.seq.n; i++)
            if (!emit_expr(cg, e->u.seq.items[i])) return 0;
        return 1;
    case EX_REPEAT:
        return emit_repeat(cg, e);
    case EX_VAR:
        lab = intern_var(cg, e->u.var.name);
        if (!lab) return 0;
        if (e->u.var.index) {
            const char *tl = list_label(cg, e->u.var.name);
            if (!emit_int_expr(cg, e->u.var.index)) return 0;
            fprintf(cg->f, "        mov     bx, ax\n");
            if (tl) {
                fprintf(cg->f, "        shl     bx, 1\n"
                               "        mov     si, %s\n"
                               "        add     si, bx\n"
                               "        lodsw\n"
                               "        mov     si, ax\n", tl);
            } else {
                fprintf(cg->f, "        mov     si, %s+2\n"
                               "        call    str_at\n"
                               "        call    append_char_at\n", lab);
                return 1;
            }
            fprintf(cg->f, "        call    append_str\n");
            return 1;
        }
        fprintf(cg->f, "        mov     si, %s+2\n        call    append_str\n", lab);
        return 1;
    case EX_LEN:
        if (!e->u.inner || e->u.inner->kind != EX_VAR) {
            seterr(cg, "len: пока поддерживаются только переменные");
            return 0;
        }
        lab = intern_var(cg, e->u.inner->u.var.name);
        if (!lab) return 0;
        fprintf(cg->f, "        mov     si, %s+2\n        call    append_len\n", lab);
        return 1;
    default:
        seterr(cg, "выражение пока не поддерживается кодогенератором");
        return 0;
    }
}

static int emit_print(Cg *cg, Stmt *s)
{
    int i;
    fprintf(cg->f, "        mov     word [scratch_len], 0\n");
    for (i = 0; i < s->u.print.n; i++)
        if (!emit_expr(cg, s->u.print.args[i])) return 0;
    if (s->u.print.coords) {
        Expr *x = s->u.print.args[0], *y = s->u.print.args[1];
        if (x->kind != EX_INT || y->kind != EX_INT) {
            seterr(cg, "координаты print должны быть числами");
            return 0;
        }
        fprintf(cg->f, "        mov     dl, %ld\n        mov     dh, %ld\n"
                       "        mov     si, scratch\n        call    print_at\n",
                x->u.i, y->u.i);
    } else {
        fprintf(cg->f, "        mov     si, scratch\n        call    puts\n");
    }
    return 1;
}

static int emit_input(Cg *cg, Stmt *s)
{
    char *lab = intern_var(cg, s->u.input.name);
    if (!lab) return 0;
    if (s->u.input.prompt) {
        fprintf(cg->f, "        mov     word [scratch_len], 0\n");
        if (!emit_expr(cg, s->u.input.prompt)) return 0;
        fprintf(cg->f, "        mov     si, scratch\n        call    puts\n");
    }
    if (s->u.input.limit && s->u.input.limit->kind == EX_INT)
        fprintf(cg->f, "        mov     byte [%s], %ld\n", lab, s->u.input.limit->u.i);
    fprintf(cg->f, "        mov     dx, %s\n        call    input_line\n", lab);
    return 1;
}

static int emit_set(Cg *cg, Stmt *s)
{
    char *lab = intern_var(cg, s->u.set.name);
    if (!lab) return 0;
    if (s->u.set.value->kind == EX_LIST) {
        Expr *v = s->u.set.value;
        if (!intern_list(cg, s->u.set.name, v->u.seq.n, v->u.seq.items)) return 0;
        fprintf(cg->f, "        mov     word [scratch_len], 0\n"
                       "        mov     ax, %d\n"
                       "        call    append_int\n"
                       "        mov     si, scratch\n"
                       "        mov     di, %s+2\n"
                       "        call    strcpy_ds\n", v->u.seq.n, lab);
        return 1;
    }
    fprintf(cg->f, "        mov     word [scratch_len], 0\n");
    if (!emit_expr(cg, s->u.set.value)) return 0;
    fprintf(cg->f, "        mov     si, scratch\n"
                   "        mov     di, %s+2\n"
                   "        call    strcpy_ds\n", lab);
    return 1;
}

/* Условие: переход на false_label, если условие ЛОЖНО. */
static int emit_cond(Cg *cg, Expr *e, const char *fl)
{
    const char *op;
    int numeric;
    if (!e || e->kind != EX_BIN) {
        fprintf(cg->f, "        mov     word [scratch_len], 0\n");
        if (!emit_expr(cg, e)) return 0;
        fprintf(cg->f, "        cmp     byte [scratch], 0\n"
                       "        je      %s\n", fl);
        return 1;
    }
    op = e->u.bin.op;
    if (strcmp(op, "and") == 0 || strcmp(op, "or") == 0) {
        seterr(cg, "and/or в скомпилированном if пока не поддерживаются");
        return 0;
    }
    numeric = e->u.bin.a->kind != EX_STR && e->u.bin.a->kind != EX_CONCAT &&
              e->u.bin.b->kind != EX_STR && e->u.bin.b->kind != EX_CONCAT;
    if (numeric) {
        if (!emit_int_expr(cg, e->u.bin.a)) return 0;
        fprintf(cg->f, "        push    ax\n");
        if (!emit_int_expr(cg, e->u.bin.b)) return 0;
        fprintf(cg->f, "        mov     bx, ax\n        pop     ax\n"
                       "        cmp     ax, bx\n");
    } else {
        fprintf(cg->f, "        mov     word [scratch_len], 0\n");
        if (!emit_expr(cg, e->u.bin.a)) return 0;
        fprintf(cg->f, "        mov     si, scratch\n"
                       "        mov     di, tmp1\n"
                       "        call    strcpy_ds\n"
                       "        mov     word [scratch_len], 0\n");
        if (!emit_expr(cg, e->u.bin.b)) return 0;
        fprintf(cg->f, "        mov     si, tmp1\n"
                       "        mov     di, scratch\n"
                       "        call    cmp_str\n"
                       "        cmp     ax, 0\n");
    }
    if      (strcmp(op,"==")==0) fprintf(cg->f, "        jne     near %s\n", fl);
    else if (strcmp(op,"!=")==0) fprintf(cg->f, "        je      near %s\n", fl);
    else if (strcmp(op,">") ==0) fprintf(cg->f, "        jle     near %s\n", fl);
    else if (strcmp(op,"<") ==0) fprintf(cg->f, "        jge     near %s\n", fl);
    else if (strcmp(op,">=")==0) fprintf(cg->f, "        jl      near %s\n", fl);
    else if (strcmp(op,"<=")==0) fprintf(cg->f, "        jg      near %s\n", fl);
    else { seterr(cg, "неизвестный оператор условия"); return 0; }
    return 1;
}

static int emit_if(Cg *cg, Stmt *s)
{
    int i, end = cg->uid++;
    char lbl[32];
    for (i = 0; i < s->u.cond.n; i++) {
        int fl = cg->uid++;
        sprintf(lbl, ".iff%d", fl);
        if (!emit_cond(cg, s->u.cond.br[i].cond, lbl)) return 0;
        if (!emit_block(cg, s->u.cond.br[i].body)) return 0;
        fprintf(cg->f, "        jmp     near .ife%d\n", end);
        fprintf(cg->f, "%s:\n", lbl);
    }
    if (s->u.cond.els && !emit_block(cg, s->u.cond.els)) return 0;
    fprintf(cg->f, ".ife%d:\n", end);
    return 1;
}

static int emit_cycle(Cg *cg, Stmt *s)
{
    int id = cg->uid++;
    char endlbl[32];
    sprintf(endlbl, ".cye%d", id);
    /* CX сохраняется: вложенные циклы и sc не должны портить счётчик внешнего. */
    fprintf(cg->f, "        push    cx\n");
    if (s->u.cycle.count) {
        if (!emit_int_expr(cg, s->u.cycle.count)) return 0;
        fprintf(cg->f, "        mov     cx, ax\n");
    }
    fprintf(cg->f, ".cyc%d:\n", id);
    if (cg->nloop >= 32) { seterr(cg, "слишком глубокие циклы"); return 0; }
    cg->loop_end[cg->nloop++] = id;
    if (!emit_block(cg, s->u.cycle.body)) return 0;
    cg->nloop--;
    if (s->u.cycle.count)
        fprintf(cg->f, "        dec     cx\n        jnz     near .cyc%d\n", id);
    else
        fprintf(cg->f, "        jmp     near .cyc%d\n", id);
    fprintf(cg->f, "%s:\n", endlbl);
    fprintf(cg->f, "        pop     cx\n");
    return 1;
}

static int emit_stmt(Cg *cg, Stmt *s)
{
    switch (s->kind) {
    case ST_PRINT: return emit_print(cg, s);
    case ST_INPUT: return emit_input(cg, s);
    case ST_SET:   return emit_set(cg, s);
    case ST_IF:    return emit_if(cg, s);
    case ST_CLEAR:
        fprintf(cg->f, "        call    clrscr\n");
        return 1;
    case ST_KON:
        if (cg->nkons >= 32) { seterr(cg, "слишком много контейнеров"); return 0; }
        cg->kons[cg->nkons].name = s->u.named.name;
        cg->kons[cg->nkons].body = s->u.named.body;
        cg->nkons++;
        return 1;
    case ST_EXEC: {
        int i;
        for (i = 0; i < cg->nkons; i++)
            if (strcmp(cg->kons[i].name, s->u.exec.name) == 0)
                return emit_block(cg, cg->kons[i].body);
        seterr(cg, "exec: контейнер не найден"); return 0;
    }
    case ST_RANDOM: return emit_random(cg, s);
    case ST_CYCLE: return emit_cycle(cg, s);
    case ST_BREAK:
        if (cg->nloop == 0) { seterr(cg, "sc вне цикла"); return 0; }
        fprintf(cg->f, "        jmp     near .cye%d\n", cg->loop_end[cg->nloop-1]);
        return 1;
    default:
        seterr(cg, "команда пока не поддерживается кодогенератором "
                   "(полная поддержка -- в режиме `run`)");
        return 0;
    }
}

static int emit_block(Cg *cg, Block *b)
{
    int i;
    for (i = 0; i < b->n; i++)
        if (!emit_stmt(cg, b->items[i])) return 0;
    return 1;
}

static void emit_runtime(Cg *cg)
{
    FILE *r = fopen(KON_RUNTIME_INC, "rb");
    char buf[4096];
    size_t n;
    if (!r) {
        fprintf(cg->f, "; ВНИМАНИЕ: runtime не найден: %s\n", KON_RUNTIME_INC);
        return;
    }
    while ((n = fread(buf, 1, sizeof(buf), r)) > 0) fwrite(buf, 1, n, cg->f);
    fclose(r);
}

static void emit_data(Cg *cg)
{
    int i, j;
    fprintf(cg->f, "\nscratch:    times 256 db 0\nscratch_len dw 0\n"
                   "tmp1:       times 256 db 0\n\n");
    for (i = 0; i < cg->nvars; i++)
        fprintf(cg->f, "%s   db 255, 0\n          times 256 db 0\n",
                cg->vars[i].label);
    /* Сначала таблицы:他们 могут зарегистрировать новые строковые литералы. */
    for (i = 0; i < cg->nlists; i++) {
        fprintf(cg->f, "%s   dw ", cg->lists[i].label);
        for (j = 0; j < cg->lists[i].n; j++) {
            Expr *it = cg->lists[i].items[j];
            char *sl;
            if (j) fprintf(cg->f, ", ");
            if (it && it->kind != EX_STR && it->kind == EX_CONCAT) {
                int q;
                size_t len = 1;
                char *buf;
                Expr tmp;
                int ok = 1;
                for (q = 0; q < it->u.seq.n; q++) {
                    if (it->u.seq.items[q]->kind != EX_STR) { ok = 0; break; }
                    len += strlen(it->u.seq.items[q]->u.s);
                }
                if (ok) {
                    buf = (char *)malloc(len);
                    buf[0] = 0;
                    for (q = 0; q < it->u.seq.n; q++) strcat(buf, it->u.seq.items[q]->u.s);
                    memset(&tmp, 0, sizeof(tmp));
                    tmp.kind = EX_STR;
                    tmp.line = it->line;
                    tmp.u.s = buf;
                    sl = intern_lit(cg, &tmp);
                    free(buf);
                    fprintf(cg->f, "%s", sl ? sl : "0");
                    continue;
                }
            }
            if (!it || it->kind != EX_STR) { fprintf(cg->f, "0"); continue; }
            sl = intern_lit(cg, it);
            fprintf(cg->f, "%s", sl ? sl : "0");
        }
        fputc('\n', cg->f);
    }
    /* Литералы печатаем последними -- список мог добавить новые. */
    for (i = 0; i < cg->nlits; i++) {
        fprintf(cg->f, "%s   db ", cg->lits[i].label);
        for (j = 0; j < (int)cg->lits[i].n; j++) {
            if (j && (j % 16) == 0) fprintf(cg->f, "\n          db ");
            fprintf(cg->f, "0x%02X", cg->lits[i].bytes[j]);
            if (j + 1 < (int)cg->lits[i].n) fputc(',', cg->f);
        }
        if (cg->lits[i].n == 0) fputc('0', cg->f);
        fprintf(cg->f, ", 0\n");
    }
}

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

/* Ищем уникальный маркер в скомпилированном образе. */
static long find_marker(const unsigned char *buf, long n, const unsigned char *m)
{
    long i;
    for (i = 0; i + 4 <= n; i++)
        if (buf[i] == m[0] && buf[i+1] == m[1] && buf[i+2] == m[2] && buf[i+3] == m[3])
            return i;
    return -1;
}

static void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static int write_mz(const char *binpath, const char *outpath, char *err, size_t errsz)
{
    static const unsigned char mdata[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    static const unsigned char mstack[4] = { 0xFE, 0xED, 0xFA, 0xCE };
    static const unsigned char mdelta[4] = { 0x99, 0x88, 0x77, 0x66 };
    FILE *fb, *fo;
    unsigned char *buf;
    unsigned char hdr[64];
    long n, md, ms, mr, dpara, spara;
    unsigned total, pages, last;

    (void)errsz;
    fb = fopen(binpath, "rb");
    if (!fb) { if (err) sprintf(err, "нет скомпилированного образа"); return 0; }
    fseek(fb, 0, SEEK_END); n = ftell(fb); fseek(fb, 0, SEEK_SET);
    buf = (unsigned char *)malloc((size_t)n ? (size_t)n : 1);
    if (!buf || fread(buf, 1, (size_t)n, fb) != (size_t)n) {
        fclose(fb); free(buf); if (err) sprintf(err, "ошибка чтения образа"); return 0;
    }
    fclose(fb);

    md = find_marker(buf, n, mdata);
    ms = find_marker(buf, n, mstack);
    mr = find_marker(buf, n, mdelta);
    if (md < 0 || ms < 0 || mr < 0) {
        free(buf); if (err) sprintf(err, "маркеры сегментов не найдены"); return 0;
    }
    /* Маркер стоит в самом начале секции, а секции выровнены по абзацу. */
    /* Номера абзацев -- от начала файла (сегмент загрузки), CS уже = 4. */
    dpara = (unsigned)(md / 16) - 4;   /* смещение DATA от CS */
    spara = (unsigned)(ms / 16);       /* абзац стека от load segment */
    put16(buf + mr + 4, dpara);              /* DATA_DELTA для CS */

    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'M'; hdr[1] = 'Z';
    total = (unsigned)(n + 64);
    pages = total / 512;
    last = total % 512;
    if (last) pages++;
    put16(hdr + 2, last ? last : 512);
    put16(hdr + 4, pages);
    put16(hdr + 8, 4);                       /* 64 байта = 4 абзаца */
    put16(hdr + 10, 0);                      /* minalloc */
    put16(hdr + 12, 0xFFFF);                 /* maxalloc */
    put16(hdr + 14, spara);                  /* SS (абзац от load segment) */
    put16(hdr + 16, 4096);                   /* SP */
    put16(hdr + 20, 0);                      /* IP */
    put16(hdr + 22, 4);                      /* CS */
    put16(hdr + 24, 0x40);                   /* таблица релокаций */

    fo = fopen(outpath, "wb");
    if (!fo) { free(buf); if (err) sprintf(err, "не удалось создать .EXE"); return 0; }
    fwrite(hdr, 1, 64, fo);
    fwrite(buf, 1, (size_t)n, fo);
    fclose(fo);
    free(buf);
    return 1;
}

static int build(Program *p, const char *outpath, int exe, char *err, size_t errsz)
{
    Cg cg;
    char asmpath[4096], binpath[4096], cmd[16384], naserr[4096];
    long sz;
    int rc;

    memset(&cg, 0, sizeof(cg));
    cg.err = err; cg.errsz = errsz;
    if (err && errsz) err[0] = 0;

    sprintf(asmpath, "build/%s.asm", outpath);
    sprintf(binpath, "build/%s.bin", outpath);
    cg.f = fopen(asmpath, "w");
    if (!cg.f) { seterr(&cg, "не удалось создать build/*.asm"); return 0; }

    if (exe) {
        fprintf(cg.f, "; KonLang v5 (EXE) -- автоматически\n"
                      "section .text align=16 vstart=0\n"
                      "kon_main:\n"
                      "        cld\n"
                      "        mov     ax, cs\n"
                      "        add     ax, [cs:data_delta]\n"
                      "        mov     ds, ax\n"
                      "        db 0x99,0x88,0x77,0x66\n"
                      "data_delta dw 0\n");
    } else {
        fprintf(cg.f, "; KonLang v5 (COM) -- автоматически\n"
                      "        org 100h\n"
                      "        jmp kon_main\n"
                      "kon_main:\n"
                      "        cld\n");
    }
    if (!emit_block(&cg, p->body)) { fclose(cg.f); return 0; }
    fprintf(cg.f, "        mov     ax, 4C00h\n        int     21h\n\n");
    emit_runtime(&cg);
    if (exe) {
        fprintf(cg.f, "\nsection .data align=16 vstart=0\n"
                      "        db 0xDE,0xAD,0xBE,0xEF\n");
    }
    emit_data(&cg);
    if (exe) {
        fprintf(cg.f, "\nsection .stack align=16 vstart=0\n"
                      "        db 0xFE,0xED,0xFA,0xCE\n"
                      "        times 4096 db 0\n");
    }
    fclose(cg.f);

    sprintf(naserr, "build/nasm.err");
    sprintf(cmd, "nasm -f bin -o \"%s\" \"%s\" 2> \"%s\"",
            exe ? binpath : outpath, asmpath, naserr);
    rc = system(cmd);
    if (rc != 0) {
        FILE *e = fopen(naserr, "r");
        char buf[512];
        if (e && err && errsz) {
            size_t nn = fread(buf, 1, sizeof(buf)-1, e);
            buf[nn] = 0;
            buf[strcspn(buf, "\r\n")] = 0;
            sprintf(err, "NASM: %s", buf);
            fclose(e);
        } else if (err && errsz) {
            sprintf(err, "NASM завершился с кодом %d", rc);
        }
        return 0;
    }
    if (exe && !write_mz(binpath, outpath, err, errsz)) return 0;

    sz = file_size(outpath);
    if (sz > (exe ? 640L*1024L : (long)COM_LIMIT)) {
        remove(outpath);
        if (err && errsz)
            sprintf(err, "программа %ld байт не влезает в %s "
                         "(лимит %ld)", sz, exe ? ".EXE" : ".COM",
                    exe ? 640L*1024L : (long)COM_LIMIT);
        return 0;
    }
    printf("; сгенерировано %s (%ld байт), ассемблер: %s\n",
           outpath, sz, asmpath);
    return 1;
}

int kon_build_com(Program *p, const char *outpath, char *err, size_t errsz)
{
    return build(p, outpath, 0, err, errsz);
}

int kon_build_exe(Program *p, const char *outpath, char *err, size_t errsz)
{
    return build(p, outpath, 1, err, errsz);
}
