#define _DEFAULT_SOURCE 1
/*
 * interp.c -- интерпретатор KonLang (режим `run`).
 * Работает с тем же AST, что и кодогенератор. Типы: int (long), float (double),
 * строка (UTF-8) и список. Ошибки передаются кодом возврата, try ловит их,
 * не прибегая к longjmp.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <dirent.h>
#include "interp.h"
#include "ast.h"

#define EX_OK     0
#define EX_BREAK  1
#define EX_RETURN 2
#define EX_ERROR  3
#define EX_EXIT   4

typedef enum { V_INT, V_FLOAT, V_STR, V_LIST } VKind;
typedef struct Value Value;
struct Value {
    VKind kind;
    union {
        long i;
        double f;
        char *s;
        struct { Value **v; int n; } lst;
    } u;
};

typedef struct Binding { char *name; Value *val; struct Binding *next; } Binding;
typedef struct KonDef { char *name; Block *body; struct KonDef *next; } KonDef;
typedef struct DefFn {
    char *name; char **params; int np; Block *body; struct DefFn *next;
} DefFn;

typedef struct {
    Binding *vars;
    KonDef *kons;
    DefFn *defs;
    char err[512];
} Env;

static char *strdup_local(const char *s);
static Value *v_new(VKind k)
{
    Value *v = (Value *)calloc(1, sizeof(Value));
    if (v) v->kind = k;
    return v;
}

static Value *v_int(long x)   { Value *v = v_new(V_INT); if (v) v->u.i = x; return v; }
static Value *v_float(double x) { Value *v = v_new(V_FLOAT); if (v) v->u.f = x; return v; }

static Value *v_str(const char *s)
{
    Value *v = v_new(V_STR);
    if (v) { v->u.s = (char *)malloc(strlen(s)+1); if (v->u.s) strcpy(v->u.s, s); }
    return v;
}

static void v_free(Value *v)
{
    int i;
    if (!v) return;
    if (v->kind == V_STR) free(v->u.s);
    if (v->kind == V_LIST) {
        for (i = 0; i < v->u.lst.n; i++) v_free(v->u.lst.v[i]);
        free(v->u.lst.v);
    }
    free(v);
}

static Value *v_copy(Value *v)
{
    int i;
    Value *r;
    if (!v) return v_str("");
    switch (v->kind) {
    case V_INT: return v_int(v->u.i);
    case V_FLOAT: return v_float(v->u.f);
    case V_STR: return v_str(v->u.s);
    default:
        r = v_new(V_LIST);
        r->u.lst.v = (Value **)calloc((size_t)(v->u.lst.n ? v->u.lst.n : 1), sizeof(Value*));
        for (i = 0; i < v->u.lst.n; i++) r->u.lst.v[i] = v_copy(v->u.lst.v[i]);
        r->u.lst.n = v->u.lst.n;
        return r;
    }
}

static char *v_text(Value *v)
{
    char buf[64];
    if (!v) return (char *)calloc(1,1);
    switch (v->kind) {
    case V_INT: sprintf(buf, "%ld", v->u.i); return (char *)strdup_local(buf);
    case V_FLOAT: sprintf(buf, "%.5g", v->u.f); return (char *)strdup_local(buf);
    case V_STR: return (char *)strdup_local(v->u.s);
    default: {
        int i; size_t total = 1; char *p;
        char **parts = (char **)malloc((size_t)(v->u.lst.n+1)*sizeof(char*));
        for (i = 0; i < v->u.lst.n; i++) {
            parts[i] = v_text(v->u.lst.v[i]);
            total += strlen(parts[i]) + 1;
        }
        p = (char *)malloc(total);
        p[0] = 0;
        for (i = 0; i < v->u.lst.n; i++) {
            strcat(p, parts[i]);
            if (i + 1 < v->u.lst.n) strcat(p, "\n");
            free(parts[i]);
        }
        free(parts);
        return p;
    }
    }
}

static char *strdup_local(const char *s)
{
    size_t n = strlen(s)+1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int v_num(Value *v, double *out)
{
    if (!v) return 0;
    if (v->kind == V_INT) { *out = (double)v->u.i; return 1; }
    if (v->kind == V_FLOAT) { *out = v->u.f; return 1; }
    if (v->kind == V_STR) {
        char *end;
        double d = strtod(v->u.s, &end);
        if (end != v->u.s) { *out = d; return 1; }
    }
    return 0;
}

/* Строки внутри компилятора UTF-8: длина и индекс считают символы, не байты. */
static long utf8_count(const char *s)
{
    long n = 0;
    while (*s) { if (((unsigned char)*s & 0xC0) != 0x80) n++; s++; }
    return n;
}

static char *utf8_char(const char *s, long idx)
{
    long cur = -1;
    const char *p = s;
    size_t len;
    char *out;
    while (*p) {
        if (((unsigned char)*p & 0xC0) != 0x80) cur++;
        if (cur == idx) break;
        p++;
    }
    if (!*p) return strdup_local("");
    len = 1;
    while (p[len] && ((unsigned char)p[len] & 0xC0) == 0x80) len++;
    out = (char *)malloc(len + 1);
    memcpy(out, p, len);
    out[len] = 0;
    return out;
}

static int v_truth(Value *v)
{
    if (!v) return 0;
    if (v->kind == V_INT) return v->u.i != 0;
    if (v->kind == V_FLOAT) return fabs(v->u.f) > 1e-12;
    if (v->kind == V_STR) return v->u.s[0] != 0;
    return v->u.lst.n > 0;
}

static Value *env_get(Env *e, const char *name)
{
    Binding *b;
    for (b = e->vars; b; b = b->next)
        if (strcmp(b->name, name) == 0) return b->val;
    return NULL;
}

static void env_set(Env *e, const char *name, Value *val)
{
    Binding *b;
    for (b = e->vars; b; b = b->next) {
        if (strcmp(b->name, name) == 0) {
            if (b->val == val) return;
            v_free(b->val);
            b->val = val;
            return;
        }
    }
    b = (Binding *)malloc(sizeof(Binding));
    b->name = strdup_local(name);
    b->val = val;
    b->next = e->vars;
    e->vars = b;
}

static void env_fail(Env *e, const char *fmt, const char *arg)
{
    if (!e->err[0]) {
        if (arg) sprintf(e->err, fmt, arg);
        else sprintf(e->err, "%s", fmt);
    }
}

static int eval(Env *, Expr *, Value **);
static int exec_block(Env *, Block *, Value **);
static int exec_stmt(Env *, Stmt *, Value **);

/* ---------- арифметика ---------- */

static int arith(Env *e, Expr *x, double *out, int *all_int)
{
    Value *v = NULL;
    double a, b;
    int ia = 1, ib = 1;
    if (!x) return 0;
    if (x->kind == EX_BIN) {
        if (!arith(e, x->u.bin.a, &a, &ia)) return 0;
        if (!arith(e, x->u.bin.b, &b, &ib)) return 0;
        *all_int = ia && ib;
        if (strcmp(x->u.bin.op, "+") == 0) *out = a + b;
        else if (strcmp(x->u.bin.op, "-") == 0) *out = a - b;
        else if (strcmp(x->u.bin.op, "*") == 0) *out = a * b;
        else if (strcmp(x->u.bin.op, "/") == 0) {
            if (fabs(b) < 1e-12) { env_fail(e, "math: деление на ноль", NULL); return 0; }
            *out = a / b;
            *all_int = 0;
        } else { env_fail(e, "math: неизвестная операция", NULL); return 0; }
        return 1;
    }
    if (x->kind == EX_INT) { *out = (double)x->u.i; *all_int = 1; return 1; }
    if (x->kind == EX_FLOAT) { *out = x->u.f; *all_int = 0; return 1; }
    if (x->kind == EX_LEN) {
        if (!eval(e, x, &v)) return 0;
        *out = (double)(v->kind == V_STR ? (long)strlen(v->u.s) :
                        v->kind == V_LIST ? (long)v->u.lst.n : 0L);
        *all_int = 1;
        v_free(v);
        return 1;
    }
    if (!eval(e, x, &v)) return 0;
    if (!v_num(v, out)) { env_fail(e, "math: операнд не число", NULL); v_free(v); return 0; }
    *all_int = v->kind == V_INT;
    v_free(v);
    return 1;
}

static int eval_math(Env *e, Expr *x, Value **out)
{
    double d = 0;
    int ai;
    const char *t = x->u.math.type;
    if (!arith(e, x->u.math.a, &d, &ai)) return 0;
    if (t && strcmp(t, "int") == 0) *out = v_int((long)d);
    else *out = v_float(d);
    return 1;
}

/* ---------- выражения ---------- */

static int eval(Env *e, Expr *x, Value **out)
{
    int i;
    *out = NULL;
    if (!x) return 1;
    switch (x->kind) {
    case EX_INT:  *out = v_int(x->u.i); return 1;
    case EX_FLOAT: *out = v_float(x->u.f); return 1;
    case EX_STR:  *out = v_str(x->u.s); return 1;
    case EX_MATH:
        if (!eval_math(e, x, out)) return 0;
        return 1;
    case EX_LIST: {
        Value *v = v_new(V_LIST);
        Value **items = (Value **)calloc((size_t)(x->u.seq.n ? x->u.seq.n : 1), sizeof(Value*));
        for (i = 0; i < x->u.seq.n; i++) {
            if (!eval(e, x->u.seq.items[i], &items[i])) {
                int j; for (j = 0; j < i; j++) v_free(items[j]);
                free(items); v_free(v); return 0;
            }
        }
        v->u.lst.v = items; v->u.lst.n = x->u.seq.n;
        *out = v;
        return 1;
    }
    case EX_CONCAT: {
        size_t total = 1;
        char *buf, *p;
        for (i = 0; i < x->u.seq.n; i++) {
            Value *part = NULL;
            char *s;
            if (!eval(e, x->u.seq.items[i], &part)) return 0;
            s = v_text(part);
            total += strlen(s);
            free(s);
            v_free(part);
        }
        buf = (char *)malloc(total);
        buf[0] = 0; p = buf;
        for (i = 0; i < x->u.seq.n; i++) {
            Value *part = NULL;
            char *s;
            if (!eval(e, x->u.seq.items[i], &part)) { free(buf); return 0; }
            s = v_text(part);
            strcpy(p, s); p += strlen(s);
            free(s); v_free(part);
        }
        *out = v_str(buf);
        free(buf);
        return 1;
    }
    case EX_REPEAT: {
        Value *txt = NULL, *cnt = NULL;
        char *s, *buf;
        long n, k;
        double dn = 0;
        size_t len;
        if (!eval(e, x->u.rep.text, &txt)) return 0;
        if (!eval(e, x->u.rep.count, &cnt)) { v_free(txt); return 0; }
        if (!v_num(cnt, &dn)) dn = 0;
        n = (long)dn;
        if (n < 0) n = 0;
        s = v_text(txt);
        len = strlen(s);
        buf = (char *)malloc(len * (size_t)n + 1);
        buf[0] = 0;
        for (k = 0; k < n; k++) strcat(buf, s);
        *out = v_str(buf);
        free(buf); free(s); v_free(txt); v_free(cnt);
        return 1;
    }
    case EX_LEN: {
        Value *v = NULL;
        long n = 0;
        if (!eval(e, x->u.inner, &v)) return 0;
        if (v->kind == V_STR) n = utf8_count(v->u.s);
        else if (v->kind == V_LIST) n = v->u.lst.n;
        else { env_fail(e, "len: аргумент должен быть строкой или списком", NULL); v_free(v); return 0; }
        v_free(v);
        *out = v_int(n);
        return 1;
    }
    case EX_VAR: {
        Value *v = env_get(e, x->u.var.name);
        if (!v) { env_fail(e, "неизвестная переменная [...]", NULL); return 0; }
        if (!x->u.var.index) { *out = v_copy(v); return 1; }
        {
            Value *idx = NULL, *res = NULL;
            long k;
            double dk = -1;
            if (!eval(e, x->u.var.index, &idx)) return 0;
            if (!v_num(idx, &dk)) dk = -1;
            k = (long)dk;
            if (v->kind == V_LIST) {
                if (k < 0 || k >= v->u.lst.n) {
                    env_fail(e, "индекс за границами списка", NULL);
                    v_free(idx); return 0;
                }
                res = v->u.lst.v[k];
            } else if (v->kind == V_STR) {
                if (k < 0 || k >= utf8_count(v->u.s)) {
                    env_fail(e, "индекс за границами строки", NULL);
                    v_free(idx); return 0;
                }
                { char *ch = utf8_char(v->u.s, k); res = v_str(ch); free(ch); }
            } else {
                env_fail(e, "индексация неприменима", NULL);
                v_free(idx); return 0;
            }
            v_free(idx);
            if (res && v->kind != V_STR) {
                /* копируем элемент списка: нельзя отдавать владельца наружу */
                char *s = v_text(res);
                *out = v_str(s);
                free(s);
            } else *out = res;
            return 1;
        }
    }
    case EX_BIN: {
        const char *op = x->u.bin.op;
        Value *a = NULL, *b = NULL;
        if (!eval(e, x->u.bin.a, &a)) return 0;
        if (!eval(e, x->u.bin.b, &b)) { v_free(a); return 0; }
        if (strcmp(op, "and") == 0 || strcmp(op, "or") == 0) {
            int r = strcmp(op, "and") == 0 ? (v_truth(a) && v_truth(b)) : (v_truth(a) || v_truth(b));
            v_free(a); v_free(b); *out = v_int(r); return 1;
        }
        {
            int cmp = 0;
            double da = 0, db = 0;
            if (a->kind == V_STR && b->kind == V_STR) cmp = strcmp(a->u.s, b->u.s);
            else {
                if (!v_num(a, &da)) da = 0;
                if (!v_num(b, &db)) db = 0;
                cmp = da < db ? -1 : (da > db ? 1 : 0);
            }
            if      (strcmp(op, "==") == 0) cmp = cmp == 0;
            else if (strcmp(op, "!=") == 0) cmp = cmp != 0;
            else if (strcmp(op, ">") == 0)  cmp = cmp > 0;
            else if (strcmp(op, "<") == 0)  cmp = cmp < 0;
            else if (strcmp(op, ">=") == 0) cmp = cmp >= 0;
            else if (strcmp(op, "<=") == 0) cmp = cmp <= 0;
            else { v_free(a); v_free(b); env_fail(e, "неизвестный оператор условия", NULL); return 0; }
            v_free(a); v_free(b);
            *out = v_int(cmp != 0);
            return 1;
        }
    }
    case EX_CALL: {
        DefFn *f;
        /* openFile / searchFile используются и как выражения в print/set. */
        if (strcmp(x->u.call.name, "openFile") == 0 ||
            strcmp(x->u.call.name, "searchFile") == 0) {
            Stmt tmp;
            Expr arg;
            Value *res;
            int is_open = strcmp(x->u.call.name, "openFile") == 0;
            int code;
            memset(&tmp, 0, sizeof(tmp));
            memset(&arg, 0, sizeof(arg));
            tmp.kind = ST_FILE;
            tmp.line = x->line;
            tmp.u.file.op = is_open ? KF_OPEN : KF_SEARCH;
            tmp.u.file.a = &arg;
            arg.kind = EX_STR;
            arg.line = x->line;
            arg.u.s = x->u.call.n ? x->u.call.args[0]->u.s : (char *)"";
            if (arg.u.s == NULL) arg.u.s = (char *)"";
            code = exec_stmt(e, &tmp, out);
            if (code != EX_OK) return 0;
            res = env_get(e, is_open ? "data" : "files");
            *out = v_copy(res);
            return 1;
        }
        for (f = e->defs; f; f = f->next)
            if (strcmp(f->name, x->u.call.name) == 0) break;
        if (!f) { env_fail(e, "неизвестная функция", NULL); return 0; }
        {
            Value **old = (Value **)calloc((size_t)(f->np ? f->np : 1), sizeof(Value*));
            int i2, code;
            for (i2 = 0; i2 < f->np; i2++) {
                Binding *b = NULL, *q;
                for (q = e->vars; q; q = q->next)
                    if (strcmp(q->name, f->params[i2]) == 0) { b = q; break; }
                old[i2] = b ? v_copy(b->val) : NULL;   /* Deep copy: тело функции может освободить исходник. */
                {
                    Value *arg = NULL;
                    if (i2 < x->u.call.n) {
                        if (!eval(e, x->u.call.args[i2], &arg)) { free(old); return 0; }
                    } else arg = v_str("");
                    env_set(e, f->params[i2], arg);
                }
            }
            code = exec_block(e, f->body, out);
            for (i2 = 0; i2 < f->np; i2++) env_set(e, f->params[i2], old[i2] ? old[i2] : v_str(""));
            free(old);
            if (code == EX_RETURN) return 1;
            if (code == EX_ERROR) return 0;
            if (!*out) *out = v_str("");
            return 1;
        }
    }
    default:
        env_fail(e, "выражение не поддерживается интерпретатором", NULL);
        return 0;
    }
}

/* ---------- операторы ---------- */

static int print_args(Env *e, Expr **args, int n, int coords)
{
    int i;
    char *buf;
    size_t total = 1;
    char *p;
    if (coords) printf("\033[%ld;%ldH", args[1]->u.i + 1, args[0]->u.i + 1);
    for (i = coords ? 2 : 0; i < n; i++) {
        Value *v = NULL;
        char *s;
        if (!eval(e, args[i], &v)) return EX_ERROR;
        s = v_text(v);
        total += strlen(s);
        free(s); v_free(v);
    }
    buf = (char *)malloc(total);
    buf[0] = 0; p = buf;
    for (i = coords ? 2 : 0; i < n; i++) {
        Value *v = NULL;
        char *s;
        if (!eval(e, args[i], &v)) { free(buf); return EX_ERROR; }
        s = v_text(v);
        strcpy(p, s); p += strlen(s);
        free(s); v_free(v);
    }
    fputs(buf, stdout);
    free(buf);
    return EX_OK;
}

static int do_input(Env *e, Stmt *s)
{
    char line[1024];
    Value *prompt = NULL;
    char *text;
    size_t n;
    int limit = 255;
    if (s->u.input.prompt) {
        if (!eval(e, s->u.input.prompt, &prompt)) return EX_ERROR;
        text = v_text(prompt);
        fputs(text, stdout);
        free(text); v_free(prompt);
        fflush(stdout);
    }
    if (!fgets(line, sizeof(line), stdin)) { env_fail(e, "input: конец ввода", NULL); return EX_ERROR; }
    n = strlen(line);
    while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
    if (s->u.input.limit) {
        Value *lv = NULL;
        if (!eval(e, s->u.input.limit, &lv)) return EX_ERROR;
        limit = (int)lv->u.i;
        v_free(lv);
        if (limit >= 0 && n > (size_t)limit) { line[limit] = 0; n = (size_t)limit; }
    }
    if (s->u.input.type == 1) {
        char *end;
        long val = strtol(line, &end, 10);
        while (*end == ' ' || *end == '\t') end++;
        if (*end) { env_fail(e, "не целое число", NULL); return EX_ERROR; }
        env_set(e, s->u.input.name, v_int(val));
    } else if (s->u.input.type == 2) {
        char *end;
        double val = strtod(line, &end);
        while (*end == ' ' || *end == '\t') end++;
        if (*end) { env_fail(e, "не число", NULL); return EX_ERROR; }
        env_set(e, s->u.input.name, v_float(val));
    } else {
        env_set(e, s->u.input.name, v_str(line));
    }
    return EX_OK;
}

static int exec_stmt(Env *e, Stmt *s, Value **retval);

static int exec_block(Env *e, Block *b, Value **retval)
{
    int i, code = EX_OK;
    if (!b) return EX_OK;
    for (i = 0; i < b->n; i++) {
        code = exec_stmt(e, b->items[i], retval);
        if (code != EX_OK) return code;
    }
    return EX_OK;
}

static int exec_stmt(Env *e, Stmt *s, Value **retval)
{
    switch (s->kind) {
    case ST_PRINT:
        return print_args(e, s->u.print.args, s->u.print.n, s->u.print.coords);
    case ST_INPUT:
        return do_input(e, s);
    case ST_SET: {
        Value *v = NULL;
        if (!eval(e, s->u.set.value, &v)) return EX_ERROR;
        env_set(e, s->u.set.name, v);
        return EX_OK;
    }
    case ST_CYCLE: {
        long i = 0, max = s->u.cycle.infinite ? 1000000 : 0;
        if (s->u.cycle.count) {
            Value *v = NULL;
            double dm = 0;
            if (!eval(e, s->u.cycle.count, &v)) return EX_ERROR;
            if (!v_num(v, &dm)) dm = 0;
            max = (long)dm;
            v_free(v);
        }
        for (i = 0; max == 0 || i < max; i++) {
            int code = exec_block(e, s->u.cycle.body, retval);
            if (code == EX_BREAK) return EX_OK;
            if (code != EX_OK) return code;
        }
        return EX_OK;
    }
    case ST_IF: {
        int i;
        for (i = 0; i < s->u.cond.n; i++) {
            Value *v = NULL;
            int code;
            if (!eval(e, s->u.cond.br[i].cond, &v)) return EX_ERROR;
            code = v_truth(v);
            v_free(v);
            if (code) return exec_block(e, s->u.cond.br[i].body, retval);
        }
        if (s->u.cond.els) return exec_block(e, s->u.cond.els, retval);
        return EX_OK;
    }
    case ST_KON: {
        KonDef *d = (KonDef *)malloc(sizeof(KonDef));
        d->name = strdup_local(s->u.named.name);
        d->body = s->u.named.body;
        d->next = e->kons;
        e->kons = d;
        return EX_OK;
    }
    case ST_DEF: {
        DefFn *f = (DefFn *)calloc(1, sizeof(DefFn));
        f->name = strdup_local(s->u.named.name);
        f->body = s->u.named.body;
        f->np = s->u.named.nparams;
        f->params = s->u.named.params;
        f->next = e->defs;
        e->defs = f;
        return EX_OK;
    }
    case ST_EXEC: {
        KonDef *d;
        for (d = e->kons; d; d = d->next)
            if (strcmp(d->name, s->u.exec.name) == 0) break;
        if (!d) { env_fail(e, "контейнер не найден", NULL); return EX_ERROR; }
        return exec_block(e, d->body, retval);
    }
    case ST_CLEAR:
        fputs("\033[2J\033[H", stdout);
        return EX_OK;
    case ST_BREAK: return EX_BREAK;
    case ST_RET: {
        if (s->u.ret.value) {
            v_free(*retval);
            if (!eval(e, s->u.ret.value, retval)) return EX_ERROR;
        }
        return EX_RETURN;
    }
    case ST_EXIT: return EX_EXIT;
    case ST_RANDOM: {
        int k = s->u.rnd.n ? (int)(random() % s->u.rnd.n) : 0;
        Value *v = NULL;
        if (!s->u.rnd.n || !eval(e, s->u.rnd.items[k], &v)) return EX_ERROR;
        env_set(e, s->u.rnd.name, v);
        return EX_OK;
    }
    case ST_KEY: {
        int c = getchar();
        char buf[2];
        if (c == EOF) c = 0;
        buf[0] = (char)c; buf[1] = 0;
        env_set(e, s->u.key.name, v_str(buf));
        return EX_OK;
    }
    case ST_TIME: {
        Value *v = NULL;
        long units = 0;
        if (!eval(e, s->u.expr, &v)) return EX_ERROR;
        if (v->kind == V_INT) units = v->u.i;
        v_free(v);
        if (units > 0) usleep((unsigned int)(units * 10000));
        return EX_OK;
    }
    case ST_FILE: {
        Value *va = NULL, *vb = NULL;
        char *pa = NULL, *pb = NULL;
        FILE *f;
        if (s->u.file.a && !eval(e, s->u.file.a, &va)) return EX_ERROR;
        if (va) { pa = v_text(va); v_free(va); }
        if (s->u.file.b && !eval(e, s->u.file.b, &vb)) { free(pa); return EX_ERROR; }
        if (vb) { pb = v_text(vb); v_free(vb); }
        if (!pa) { env_fail(e, "путь к файлу не задан", NULL); return EX_ERROR; }
        switch (s->u.file.op) {
        case KF_OPEN:
            f = fopen(pa, "rb");
            if (!f) {
                env_fail(e, "openFile: нет такого файла", NULL);
                free(pa); free(pb); return EX_ERROR;
            }
            {
                char *data;
                long n;
                fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
                data = (char *)malloc((size_t)n + 1);
                if (n > 0 && fread(data, 1, (size_t)n, f) != (size_t)n) {
                    free(data); fclose(f); free(pa); free(pb);
                    env_fail(e, "openFile: ошибка чтения", NULL); return EX_ERROR;
                }
                data[n] = 0;
                fclose(f);
                env_set(e, "data", v_str(data));
                free(data);
            }
            free(pa); free(pb);
            return EX_OK;
        case KF_WRITE:
            f = fopen(pa, "wb");
            if (!f) { env_fail(e, "writeFile: нет прав", NULL); free(pa); free(pb); return EX_ERROR; }
            if (pb) fputs(pb, f);
            fclose(f); free(pa); free(pb);
            return EX_OK;
        case KF_APPEND:
            f = fopen(pa, "ab");
            if (!f) { env_fail(e, "appendFile: нет прав", NULL); free(pa); free(pb); return EX_ERROR; }
            if (pb) fputs(pb, f);
            fclose(f); free(pa); free(pb);
            return EX_OK;
        case KF_DEL:
            if (remove(pa) != 0) {
                env_fail(e, "delFile: файл не найден", NULL);
                free(pa); free(pb); return EX_ERROR;
            }
            free(pa); free(pb);
            return EX_OK;
        case KF_SEARCH: {
            DIR *d = opendir(".");
            struct dirent *de;
            Value *lst = v_new(V_LIST);
            Value **items = NULL;
            int cnt = 0;
            if (!d) {
                env_fail(e, "searchFile: нет каталога", NULL);
                v_free(lst); free(pa); free(pb); return EX_ERROR;
            }
            while ((de = readdir(d)) != NULL) {
                Value **tmp;
                if (de->d_name[0] == '.') continue;
                tmp = (Value **)realloc(items, (size_t)(cnt+1)*sizeof(Value*));
                if (tmp) { items = tmp; items[cnt++] = v_str(de->d_name); }
            }
            closedir(d);
            lst->u.lst.v = items; lst->u.lst.n = cnt;
            env_set(e, "files", lst);
            free(pa); free(pb);
            return EX_OK;
        }
        }
        free(pa); free(pb);
        return EX_OK;
    }
    case ST_SCREEN:
    case ST_TOUCH: {
        int i;
        if (s->kind == ST_SCREEN && s->u.io.op == KS_SIZE) {
            env_set(e, "scr_w", v_int(80));
            env_set(e, "scr_h", v_int(25));
        }
        if (s->kind == ST_TOUCH) {
            env_set(e, "touch", v_int(0));
            env_set(e, "cur_x", v_int(0));
            env_set(e, "cur_y", v_int(0));
            for (i = 0; i < s->u.io.n; i++) {
                if (s->u.io.args[i]->kind == EX_STR) {
                    char nm[64];
                    sprintf(nm, "hit_%s", s->u.io.args[i]->u.s);
                    env_set(e, nm, v_int(0));
                }
            }
        }
        return EX_OK;
    }
    case ST_TRY: {
        int code = exec_block(e, s->u.tryc.body, retval);
        if (code == EX_ERROR) {
            char saved[512];
            strcpy(saved, e->err[0] ? e->err : "ошибка");
            e->err[0] = 0;
            env_set(e, "err", v_str(saved));
            if (s->u.tryc.handler) {
                KonDef *d;
                for (d = e->kons; d; d = d->next)
                    if (strcmp(d->name, s->u.tryc.handler) == 0) break;
                if (!d) { env_fail(e, "контейнер catch не найден", NULL); return EX_ERROR; }
                return exec_block(e, d->body, retval);
            }
            if (s->u.tryc.catchblk) return exec_block(e, s->u.tryc.catchblk, retval);
            return EX_OK;
        }
        return code;
    }
    default:
        env_fail(e, "команда не поддерживается интерпретатором", NULL);
        return EX_ERROR;
    }
}

int kon_run(Program *p, char *err, size_t errsz)
{
    Env env;
    Value *retval = NULL;
    int code;
    memset(&env, 0, sizeof(env));
    if (err && errsz) err[0] = 0;
    code = exec_block(&env, p->body, &retval);
    if (code == EX_ERROR) {
        if (err && errsz) {
            sprintf(err, "%s", env.err[0] ? env.err : "ошибка выполнения");
        }
    } else if (err && errsz) err[0] = 0;
    v_free(retval);
    return code == EX_ERROR ? 0 : 1;
}
