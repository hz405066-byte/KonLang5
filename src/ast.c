/* ast.c -- конструкторы, освобождение и печать AST (для `check`). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

char *ast_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

Expr *ast_expr(ExprKind k, int line)
{
    Expr *e = (Expr *)calloc(1, sizeof(Expr));
    if (e) { e->kind = k; e->line = line; }
    return e;
}

Stmt *ast_stmt(StmtKind k, int line)
{
    Stmt *s = (Stmt *)calloc(1, sizeof(Stmt));
    if (s) { s->kind = k; s->line = line; }
    return s;
}

Block *ast_block(void)
{
    return (Block *)calloc(1, sizeof(Block));
}

void ast_block_add(Block *b, Stmt *s)
{
    Stmt **v;
    if (!b || !s) return;
    v = (Stmt **)realloc(b->items, (size_t)(b->n + 1) * sizeof(Stmt *));
    if (v) { b->items = v; b->items[b->n++] = s; }
}

void ast_free_expr(Expr *e)
{
    int i;
    if (!e) return;
    switch (e->kind) {
    case EX_STR:
        free(e->u.s);
        break;
    case EX_VAR:
        free(e->u.var.name);
        ast_free_expr(e->u.var.index);
        break;
    case EX_LIST:
    case EX_CONCAT:
        for (i = 0; i < e->u.seq.n; i++) ast_free_expr(e->u.seq.items[i]);
        free(e->u.seq.items);
        break;
    case EX_REPEAT:
        ast_free_expr(e->u.rep.text);
        ast_free_expr(e->u.rep.count);
        break;
    case EX_MATH:
        free(e->u.math.type);
        free(e->u.math.op);
        ast_free_expr(e->u.math.a);
        ast_free_expr(e->u.math.b);
        break;
    case EX_LEN:
        ast_free_expr(e->u.inner);
        break;
    case EX_CALL:
        free(e->u.call.name);
        for (i = 0; i < e->u.call.n; i++) ast_free_expr(e->u.call.args[i]);
        free(e->u.call.args);
        break;
    case EX_BIN:
        free(e->u.bin.op);
        ast_free_expr(e->u.bin.a);
        ast_free_expr(e->u.bin.b);
        break;
    default:
        break;
    }
    free(e);
}

static void free_expr_array(Expr **v, int n)
{
    int i;
    if (!v) return;
    for (i = 0; i < n; i++) ast_free_expr(v[i]);
    free(v);
}

void ast_free_block(Block *b)
{
    int i;
    if (!b) return;
    for (i = 0; i < b->n; i++) {
        Stmt *s = b->items[i];
        if (!s) continue;
        switch (s->kind) {
        case ST_PRINT:
            free_expr_array(s->u.print.args, s->u.print.n);
            break;
        case ST_INPUT:
            free(s->u.input.name);
            ast_free_expr(s->u.input.prompt);
            ast_free_expr(s->u.input.limit);
            break;
        case ST_KEY:
            free(s->u.key.name);
            break;
        case ST_CYCLE:
            ast_free_expr(s->u.cycle.count);
            ast_free_block(s->u.cycle.body);
            break;
        case ST_IF: {
            int j;
            for (j = 0; j < s->u.cond.n; j++) {
                ast_free_expr(s->u.cond.br[j].cond);
                ast_free_block(s->u.cond.br[j].body);
            }
            free(s->u.cond.br);
            ast_free_block(s->u.cond.els);
            break;
        }
        case ST_SET:
            free(s->u.set.name);
            ast_free_expr(s->u.set.value);
            break;
        case ST_KON:
        case ST_DEF: {
            int j;
            free(s->u.named.name);
            for (j = 0; j < s->u.named.nparams; j++) free(s->u.named.params[j]);
            free(s->u.named.params);
            ast_free_block(s->u.named.body);
            break;
        }
        case ST_RET:
            ast_free_expr(s->u.ret.value);
            break;
        case ST_EXEC:
            free(s->u.exec.name);
            break;
        case ST_RANDOM: {
            int j;
            free(s->u.rnd.name);
            for (j = 0; j < s->u.rnd.n; j++) ast_free_expr(s->u.rnd.items[j]);
            free(s->u.rnd.items);
            break;
        }
        case ST_FILE:
            ast_free_expr(s->u.file.a);
            ast_free_expr(s->u.file.b);
            break;
        case ST_SCREEN:
        case ST_TOUCH: {
            int j;
            for (j = 0; j < s->u.io.n; j++) ast_free_expr(s->u.io.args[j]);
            free(s->u.io.args);
            break;
        }
        case ST_TRY:
            ast_free_block(s->u.tryc.body);
            free(s->u.tryc.handler);
            ast_free_block(s->u.tryc.catchblk);
            break;
        case ST_TIME:
        case ST_EXPR:
            ast_free_expr(s->u.expr);
            break;
        default:
            break;
        }
        free(s);
    }
    free(b->items);
    free(b);
}

void ast_free_program(Program *p)
{
    if (!p) return;
    ast_free_block(p->body);
    free(p);
}

/* ---------- печать ---------- */

static void indent(FILE *f, int n)
{
    while (n-- > 0) fputs("  ", f);
}

static void dump_expr(FILE *f, Expr *e, int lev)
{
    int i;
    if (!e) { fputs("<нет>", f); return; }
    switch (e->kind) {
    case EX_INT:  fprintf(f, "INT(%ld)", e->u.i); break;
    case EX_FLOAT: fprintf(f, "FLOAT(%.6g)", e->u.f); break;
    case EX_STR:  fprintf(f, "STR(\"%s\")", e->u.s); break;
    case EX_VAR:
        fprintf(f, "VAR(%s", e->u.var.name);
        if (e->u.var.index) { fputs(".", f); dump_expr(f, e->u.var.index, lev); }
        fputc(')', f);
        break;
    case EX_LIST:
        fputs("LIST[", f);
        for (i = 0; i < e->u.seq.n; i++) {
            if (i) fputs(", ", f);
            dump_expr(f, e->u.seq.items[i], lev);
        }
        fputc(']', f);
        break;
    case EX_CONCAT:
        fputs("CONCAT[", f);
        for (i = 0; i < e->u.seq.n; i++) {
            if (i) fputs(" + ", f);
            dump_expr(f, e->u.seq.items[i], lev);
        }
        fputc(']', f);
        break;
    case EX_REPEAT:
        fputs("REPEAT(", f);
        dump_expr(f, e->u.rep.text, lev);
        fputs(" * ", f);
        dump_expr(f, e->u.rep.count, lev);
        fputc(')', f);
        break;
    case EX_MATH:
        fprintf(f, "MATH[%s](", e->u.math.type ? e->u.math.type : "auto");
        dump_expr(f, e->u.math.a, lev);
        fputc(')', f);
        break;
    case EX_LEN:
        fputs("LEN(", f);
        dump_expr(f, e->u.inner, lev);
        fputc(')', f);
        break;
    case EX_CALL:
        fprintf(f, "CALL(%s", e->u.call.name);
        for (i = 0; i < e->u.call.n; i++) {
            fputs(", ", f);
            dump_expr(f, e->u.call.args[i], lev);
        }
        fputc(')', f);
        break;
    case EX_BIN:
        fputs("BIN(", f);
        dump_expr(f, e->u.bin.a, lev);
        fprintf(f, " %s ", e->u.bin.op);
        dump_expr(f, e->u.bin.b, lev);
        fputc(')', f);
        break;
    }
}

static const char *stmt_name(StmtKind k)
{
    switch (k) {
    case ST_PRINT: return "print";
    case ST_INPUT: return "input";
    case ST_KEY: return "key";
    case ST_CLEAR: return "clear";
    case ST_RANDOM: return "random";
    case ST_SET: return "set";
    case ST_EXEC: return "exec";
    case ST_CYCLE: return "cycle";
    case ST_IF: return "if";
    case ST_DEF: return "def";
    case ST_RET: return "ret";
    case ST_BREAK: return "sc";
    case ST_EXIT: return "program-exit";
    case ST_TRY: return "try";
    case ST_FILE: return "file";
    case ST_SCREEN: return "screen";
    case ST_TOUCH: return "touch";
    case ST_TIME: return "time";
    case ST_KON: return "kon";
    case ST_EXPR: return "expr";
    }
    return "?";
}

static void dump_block(FILE *f, Block *b, int lev)
{
    int i, j;
    if (!b) return;
    for (i = 0; i < b->n; i++) {
        Stmt *s = b->items[i];
        indent(f, lev);
        fprintf(f, "L%-3d %s", s->line, stmt_name(s->kind));
        switch (s->kind) {
        case ST_PRINT:
            if (s->u.print.coords) fputs(" @", f);
            for (j = 0; j < s->u.print.n; j++) {
                fputs(" ", f);
                dump_expr(f, s->u.print.args[j], lev);
            }
            break;
        case ST_INPUT:
            fprintf(f, " %s%s", s->u.input.name,
                    s->u.input.type == 1 ? ":int" :
                    s->u.input.type == 2 ? ":float" : "");
            fputs(" prompt=", f);
            dump_expr(f, s->u.input.prompt, lev);
            if (s->u.input.limit) {
                fputs(" limit=", f);
                dump_expr(f, s->u.input.limit, lev);
            }
            break;
        case ST_KEY:
        case ST_EXEC:
            fprintf(f, " %s", s->kind == ST_KEY ? s->u.key.name : s->u.exec.name);
            break;
        case ST_SET:
            fprintf(f, " %s = ", s->u.set.name);
            dump_expr(f, s->u.set.value, lev);
            break;
        case ST_CYCLE:
            fprintf(f, " %s", s->u.cycle.infinite ? "inf" : "count");
            if (s->u.cycle.count) {
                fputs("=", f);
                dump_expr(f, s->u.cycle.count, lev);
            }
            fputc('\n', f);
            dump_block(f, s->u.cycle.body, lev + 1);
            indent(f, lev);
            fputs("end cycle\n", f);
            continue;
        case ST_KON:
        case ST_DEF:
            fprintf(f, " %s {\n", s->u.named.name);
            dump_block(f, s->u.named.body, lev + 1);
            indent(f, lev);
            fputs("}\n", f);
            continue;
        case ST_IF:
            for (j = 0; j < s->u.cond.n; j++) {
                if (j) { indent(f, lev); fputs("eli ", f); }
                else fputs(" [", f);
                dump_expr(f, s->u.cond.br[j].cond, lev);
                fputs("] {\n", f);
                dump_block(f, s->u.cond.br[j].body, lev + 1);
                indent(f, lev);
                fputs("}\n", f);
            }
            if (s->u.cond.els) {
                indent(f, lev);
                fputs("els {\n", f);
                dump_block(f, s->u.cond.els, lev + 1);
                indent(f, lev);
                fputs("}\n", f);
            }
            continue;
        case ST_RET:
            if (s->u.ret.value) {
                fputs(" ", f);
                dump_expr(f, s->u.ret.value, lev);
            }
            break;
        default:
            break;
        }
        fputc('\n', f);
    }
}

void ast_dump(FILE *f, Program *p)
{
    if (!p) return;
    dump_block(f, p->body, 0);
}
