/*
 * parser.c -- рекурсивный спуск.
 *
 * Грамматика (упрощённо):
 *   program   := stmt*
 *   stmt      := print | set | cycle | if | kon | exec | ret | sc | clear | ...
 *   expr      := concat          -- склейка без разделителя
 *   concat    := primary+        -- "," разделяет аргументы, "+" не склеивает
 *   math      := "math{" [type] add "}"
 *   cond      := add (("=="|"!="|"<"|">"|"<="|">="|and|or) add)*
 *
 * Важно: '{' и '[' вложены на уровне токенов, поэтому один и тот же
 * primary() разбирает и print{"x"}, и if[[i]>=3], и math{[n]*[m]}.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "lexer.h"
#include "ast.h"

typedef struct {
    Token *t;
    int n, i;
    char err[512];
} Parser;

static Token *pk(Parser *p, int k)
{
    int i = p->i + k;
    if (i < 0) i = 0;
    if (i >= p->n) i = p->n - 1;
    return &p->t[i];
}

static void adv(Parser *p)
{
    if (p->i < p->n - 1) p->i++;
}

static int is_id(Token *t, const char *s)
{
    return t->kind == TK_ID && strcmp(t->text, s) == 0;
}

static int is_word(Token *t, const char *s)
{
    return is_id(t, s) || is_id(t, "and") || is_id(t, "or") ? is_id(t, s) : 0;
}

/* Префиксная команда с суффиксом X: kon_X, input_X, def_X, random_X, key_X. */
static int cmd(Parser *p, const char *prefix, const char **suffix)
{
    Token *t = pk(p, 0);
    size_t n;
    if (t->kind != TK_ID) return 0;
    n = strlen(prefix);
    if (strncmp(t->text, prefix, n) != 0) return 0;
    if (t->text[n] == 0) { *suffix = ""; return 1; }
    if (t->text[n] == '_') { *suffix = t->text + n + 1; return 1; }
    return 0;
}

static int digits_only(const char *s)
{
    const char *q;
    if (!*s) return 0;
    for (q = s; *q; q++) if (*q < '0' || *q > '9') return 0;
    return 1;
}

static const char *describe(Token *t)
{
    static char buf[256];
    if (t->kind == TK_ID || t->kind == TK_STRING || t->kind == TK_NUMBER)
        sprintf(buf, "'%s'", t->text);
    else
        sprintf(buf, "'%s'", tok_name(t->kind));
    return buf;
}

static int perr(Parser *p, const char *msg)
{
    if (!p->err[0]) sprintf(p->err, "%ld: %s", pk(p, 0)->line, msg);
    return 0;
}

static Token *expect(Parser *p, TokKind k, const char *what)
{
    Token *t = pk(p, 0);
    if (t->kind != k) {
        if (!p->err[0])
            sprintf(p->err, "%ld: ожидается %s, найдено %s",
                    t->line, what, describe(t));
        return NULL;
    }
    return t;
}

/* Слитная запись: между командой и её '{'/'[' не должно быть пробела. */
static int glued(Parser *p)
{
    return p->i > 0 && pk(p, 0)->start == p->t[p->i-1].end;
}

static Token *expect_glued(Parser *p, TokKind k, const char *what)
{
    if (!glued(p)) { perr(p, "команда и аргументы пишутся слитно"); return NULL; }
    return expect(p, k, what);
}

static Expr *newbin(Parser *p, char *op, Expr *a, Expr *b)
{
    Expr *e = ast_expr(EX_BIN, pk(p, 0)->line);
    e->u.bin.op = op;
    e->u.bin.a = a;
    e->u.bin.b = b;
    return e;
}

/* ---------- выражения ---------- */

static Expr *parse_concat(Parser *p);
static Expr *parse_add(Parser *p);
static Expr *parse_bracket_inner(Parser *p);

static int atom_start(Token *t)
{
    if (t->kind == TK_STRING || t->kind == TK_NUMBER ||
        t->kind == TK_LBRACKET || t->kind == TK_LBRACE) return 1;
    if (t->kind == TK_ID)
        return !is_word(t, "and") && !is_word(t, "or") &&
               !is_id(t, "eli") && !is_id(t, "els") && !is_id(t, "sc");
    return 0;
}

/* Разбор вызова X{...}; имя уже прочитано. */
static Expr *parse_call_body(Parser *p, char *name, int line)
{
    Expr *e = ast_expr(EX_CALL, line);
    Expr **v = NULL;
    int n = 0;
    e->u.call.name = name;
    if (!expect(p, TK_LBRACE, "{")) { ast_free_expr(e); return NULL; }
    adv(p);
    if (pk(p, 0)->kind != TK_RBRACE) {
        for (;;) {
            Expr *a = parse_add(p);
            Expr **tmp;
            if (!a) { ast_free_expr(e); return NULL; }
            tmp = (Expr **)realloc(v, (size_t)(n+1)*sizeof(Expr*));
            if (!tmp) { ast_free_expr(a); ast_free_expr(e); return NULL; }
            v = tmp; v[n++] = a;
            if (pk(p, 0)->kind != TK_COMMA) break;
            adv(p);
        }
    }
    if (!expect(p, TK_RBRACE, "}")) { ast_free_expr(e); return NULL; }
    adv(p);
    e->u.call.args = v;
    e->u.call.n = n;
    return e;
}

static Expr *parse_math(Parser *p)
{
    Expr *e = ast_expr(EX_MATH, pk(p, 0)->line);
    Token *t = pk(p, 0);
    char *type = NULL;
    adv(p);                                    /* "math" */
    if (!expect(p, TK_LBRACE, "{")) { ast_free_expr(e); return NULL; }
    adv(p);
    t = pk(p, 0);
    if (t->kind == TK_ID &&
        (is_id(t,"int") || is_id(t,"float") || is_id(t,"double") || is_id(t,"char"))) {
        type = ast_strdup(t->text);
        adv(p);
    }
    e->u.math.type = type;
    e->u.math.a = parse_add(p);
    if (!e->u.math.a) { ast_free_expr(e); return NULL; }
    if (!expect(p, TK_RBRACE, "}")) { ast_free_expr(e); return NULL; }
    adv(p);
    return e;
}

static Expr *parse_len(Parser *p)
{
    Expr *e = ast_expr(EX_LEN, pk(p, 0)->line);
    adv(p);                                    /* "len" */
    if (!expect(p, TK_LBRACKET, "[")) { ast_free_expr(e); return NULL; }
    adv(p);
    e->u.inner = parse_bracket_inner(p);
    if (!e->u.inner || !expect(p, TK_RBRACKET, "]")) { ast_free_expr(e); return NULL; }
    adv(p);
    return e;
}

/* Содержимое '[' ... ']' (сама скобка уже съедена). */
static Expr *parse_bracket_inner(Parser *p)
{
    Token *t = pk(p, 0);
    if (t->kind == TK_ID) {
        char *name;
        int line;
        if (is_id(t, "len") && pk(p,1)->kind == TK_LBRACKET) return parse_len(p);
        if (is_id(t, "math") && pk(p,1)->kind == TK_LBRACE) return parse_math(p);
        name = ast_strdup(t->text);
        line = t->line;
        adv(p);
        if (pk(p, 0)->kind == TK_DOT) {
            Expr *e = ast_expr(EX_VAR, line);
            adv(p);
            e->u.var.name = name;
            e->u.var.index = parse_add(p);
            if (!e->u.var.index) { ast_free_expr(e); return NULL; }
            return e;
        }
        if (pk(p, 0)->kind == TK_LBRACE)
            return parse_call_body(p, name, line);
        {
            Expr *e = ast_expr(EX_VAR, line);
            e->u.var.name = name;
            return e;
        }
    }
    return parse_add(p);
}

static Expr *parse_primary(Parser *p)
{
    Token *t = pk(p, 0);
    Expr *e;

    if (t->kind == TK_STRING) {
        e = ast_expr(EX_STR, t->line);
        e->u.s = ast_strdup(t->text);
        adv(p);
        return e;
    }
    if (t->kind == TK_NUMBER) {
        if (strchr(t->text, '.')) {
            e = ast_expr(EX_FLOAT, t->line);
            e->u.f = atof(t->text);
        } else {
            e = ast_expr(EX_INT, t->line);
            e->u.i = atol(t->text);
        }
        adv(p);
        return e;
    }
    if (t->kind == TK_LBRACKET) {
        adv(p);
        e = parse_bracket_inner(p);
        if (!e || !expect(p, TK_RBRACKET, "]")) { ast_free_expr(e); return NULL; }
        adv(p);
        return e;
    }
    if (t->kind == TK_LBRACE) {          /* {"строка"*N} */
        adv(p);
        e = ast_expr(EX_REPEAT, t->line);
        e->u.rep.text = parse_concat(p);
        if (!e->u.rep.text || !expect(p, TK_STAR, "*")) {
            ast_free_expr(e); return NULL;
        }
        adv(p);
        e->u.rep.count = parse_concat(p);
        if (!e->u.rep.count || !expect(p, TK_RBRACE, "}")) {
            ast_free_expr(e); return NULL;
        }
        adv(p);
        return e;
    }
    if (t->kind == TK_ID) {
        if (is_id(t, "math") && pk(p,1)->kind == TK_LBRACE) return parse_math(p);
        if (is_id(t, "len")  && pk(p,1)->kind == TK_LBRACKET) return parse_len(p);
        if (pk(p,1)->kind == TK_LBRACE) {
            char *name = ast_strdup(t->text);
            int line = t->line;
            adv(p);
            return parse_call_body(p, name, line);
        }
        /* голое слово = строковый литерал (элементы списка) */
        e = ast_expr(EX_STR, t->line);
        e->u.s = ast_strdup(t->text);
        adv(p);
        return e;
    }
    perr(p, "ожидается выражение");
    return NULL;
}

static Expr *parse_concat(Parser *p)
{
    Expr **v = NULL;
    int n = 0;
    while (atom_start(pk(p, 0))) {
        Expr *a = parse_primary(p);
        Expr **tmp;
        if (!a) { ast_free_expr(v ? *v : NULL); free(v); return NULL; }
        tmp = (Expr **)realloc(v, (size_t)(n+1)*sizeof(Expr*));
        if (!tmp) { ast_free_expr(a); free(v); return NULL; }
        v = tmp; v[n++] = a;
    }
    if (n == 0) { perr(p, "ожидается выражение"); free(v); return NULL; }
    if (n == 1) { Expr *a = v[0]; free(v); return a; }
    {
        Expr *e = ast_expr(EX_CONCAT, pk(p,0)->line);
        e->u.seq.items = v;
        e->u.seq.n = n;
        return e;
    }
}

static Expr *parse_mul(Parser *p)
{
    Expr *a = parse_concat(p);
    while (pk(p,0)->kind == TK_STAR || pk(p,0)->kind == TK_SLASH) {
        Token *op = pk(p,0);
        Expr *b;
        adv(p);
        b = parse_concat(p);
        if (!b) { ast_free_expr(a); return NULL; }
        a = newbin(p, ast_strdup(op->kind == TK_STAR ? "*" : "/"), a, b);
    }
    return a;
}

static Expr *parse_add(Parser *p)
{
    Expr *a = parse_mul(p);
    while (pk(p,0)->kind == TK_PLUS || pk(p,0)->kind == TK_MINUS) {
        Token *op = pk(p,0);
        Expr *b;
        adv(p);
        b = parse_mul(p);
        if (!b) { ast_free_expr(a); return NULL; }
        a = newbin(p, ast_strdup(op->kind == TK_PLUS ? "+" : "-"), a, b);
    }
    return a;
}

static Expr *parse_cond(Parser *p)
{
    Expr *a = parse_add(p);
    for (;;) {
        Token *t = pk(p,0);
        const char *op = NULL;
        Expr *b;
        switch (t->kind) {
        case TK_EQ: op = "=="; break;
        case TK_NE: op = "!="; break;
        case TK_GT: op = ">";  break;
        case TK_LT: op = "<";  break;
        case TK_GE: op = ">="; break;
        case TK_LE: op = "<="; break;
        default:
            if (is_id(t, "and")) op = "and";
            else if (is_id(t, "or")) op = "or";
            break;
        }
        if (!op) break;
        adv(p);
        b = parse_add(p);
        if (!b) { ast_free_expr(a); return NULL; }
        a = newbin(p, ast_strdup(op), a, b);
    }
    return a;
}

/* ---------- операторы ---------- */

static Block *parse_block(Parser *p);
static Stmt *parse_stmt(Parser *p);

static Stmt *parse_print(Parser *p)
{
    Stmt *s = ast_stmt(ST_PRINT, pk(p,0)->line);
    Expr **v = NULL;
    int n = 0;
    adv(p);                                    /* "print" */
    if (!expect_glued(p, TK_LBRACE, "{")) { free(s); return NULL; }
    adv(p);
    if (pk(p,0)->kind != TK_RBRACE) {
        for (;;) {
            Expr *a = parse_concat(p);
            Expr **tmp;
            if (!a) { ast_free_block(NULL); free(s); return NULL; }
            tmp = (Expr **)realloc(v, (size_t)(n+1)*sizeof(Expr*));
            if (!tmp) { ast_free_expr(a); free(s); return NULL; }
            v = tmp; v[n++] = a;
            if (pk(p,0)->kind != TK_COMMA) break;
            adv(p);
        }
    }
    if (!expect(p, TK_RBRACE, "}")) { free(s); return NULL; }
    adv(p);
    s->u.print.args = v;
    s->u.print.n = n;
    s->u.print.coords = n >= 3 && v[0]->kind == EX_INT && v[1]->kind == EX_INT;
    return s;
}

static Stmt *parse_input(Parser *p)
{
    const char *suffix;
    Stmt *s;
    char *name;
    if (!cmd(p, "input", &suffix)) return NULL;
    s = ast_stmt(ST_INPUT, pk(p,0)->line);
    /* input_X наполняет регистр [inp_X] -- храним именно это имя. */
    name = (char *)malloc(strlen(suffix) + 5);
    if (name) { strcpy(name, "inp_"); strcat(name, suffix); }
    adv(p);                                    /* input_X */
    if (!expect_glued(p, TK_LBRACE, "{")) { free(name); free(s); return NULL; }
    adv(p);
    if (is_id(pk(p,0), "int")) { s->u.input.type = 1; adv(p); }
    else if (is_id(pk(p,0), "float") || is_id(pk(p,0), "double")) {
        s->u.input.type = 2; adv(p);
    }
    s->u.input.prompt = parse_concat(p);
    if (!s->u.input.prompt) { free(name); free(s); return NULL; }
    if (pk(p,0)->kind == TK_COMMA) {
        adv(p);
        s->u.input.limit = parse_concat(p);
        if (!s->u.input.limit) { ast_free_expr(s->u.input.prompt); free(name); free(s); return NULL; }
    }
    if (!expect(p, TK_RBRACE, "}")) {
        ast_free_expr(s->u.input.prompt); ast_free_expr(s->u.input.limit);
        free(name); free(s); return NULL;
    }
    adv(p);
    s->u.input.name = name;
    return s;
}

static Stmt *parse_set(Parser *p)
{
    Stmt *s = ast_stmt(ST_SET, pk(p,0)->line);
    Token *name;
    adv(p);                                    /* "set" */
    if (!expect_glued(p, TK_LBRACE, "{")) { free(s); return NULL; }
    adv(p);
    name = expect(p, TK_ID, "имя переменной");
    if (!name) { free(s); return NULL; }
    s->u.set.name = ast_strdup(name->text);
    adv(p);
    if (!expect(p, TK_ASSIGN, "=")) { free(s); return NULL; }
    adv(p);
    if (pk(p,0)->kind == TK_LBRACE) {          /* список {a,b,c} */
        Expr *lst = ast_expr(EX_LIST, pk(p,0)->line);
        Expr **v = NULL;
        int n = 0;
        adv(p);
        if (pk(p,0)->kind != TK_RBRACE) {
            for (;;) {
                Expr *a = parse_concat(p);
                Expr **tmp;
                if (!a) { ast_free_expr(lst); free(s); return NULL; }
                tmp = (Expr **)realloc(v, (size_t)(n+1)*sizeof(Expr*));
                if (!tmp) { ast_free_expr(a); ast_free_expr(lst); free(s); return NULL; }
                v = tmp; v[n++] = a;
                if (pk(p,0)->kind != TK_COMMA) break;
                adv(p);
            }
        }
        if (!expect(p, TK_RBRACE, "}")) { ast_free_expr(lst); free(s); return NULL; }
        adv(p);
        lst->u.seq.items = v;
        lst->u.seq.n = n;
        s->u.set.value = lst;
    } else {
        s->u.set.value = parse_concat(p);
        if (!s->u.set.value) { free(s); return NULL; }
    }
    if (!expect(p, TK_RBRACE, "}")) { free(s); return NULL; }
    adv(p);
    return s;
}

static Stmt *parse_cycle(Parser *p)
{
    Stmt *s = ast_stmt(ST_CYCLE, pk(p,0)->line);
    Token *t = pk(p,0);
    if (is_id(t, "cycle") && pk(p,1)->kind == TK_LBRACKET) {
        adv(p); adv(p);
        s->u.cycle.count = parse_bracket_inner(p);
        if (!s->u.cycle.count || !expect(p, TK_RBRACKET, "]")) {
            free(s); return NULL;
        }
        adv(p);
        if (!expect(p, TK_LBRACE, "{")) { free(s); return NULL; }
        adv(p);
    } else if (is_id(t, "cycle")) {
        s->u.cycle.infinite = 1;
        adv(p);
        if (!expect_glued(p, TK_LBRACE, "{")) { free(s); return NULL; }
        adv(p);
    } else if (strncmp(t->text, "cycle", 5) == 0 && digits_only(t->text + 5)) {
        Expr *e = ast_expr(EX_INT, t->line);
        e->u.i = atol(t->text + 5);
        s->u.cycle.count = e;
        adv(p);
        if (!expect_glued(p, TK_LBRACE, "{")) { free(s); return NULL; }
        adv(p);
    } else {
        perr(p, "ожидается cycle / cycleN / cycle[N]");
        free(s); return NULL;
    }
    s->u.cycle.body = parse_block(p);
    if (!s->u.cycle.body) { free(s); return NULL; }
    return s;
}

static Stmt *parse_if(Parser *p)
{
    Stmt *s = ast_stmt(ST_IF, pk(p,0)->line);
    IfBranch *v = NULL;
    int n = 0;
    adv(p);                                    /* "if" */
    for (;;) {
        IfBranch br;
        Block *body;
        IfBranch *tmp;
        if (!expect_glued(p, TK_LBRACKET, "[")) break;
        adv(p);
        br.cond = parse_cond(p);
        if (!br.cond) break;
        if (!expect(p, TK_RBRACKET, "]")) { ast_free_expr(br.cond); break; }
        adv(p);
        if (!expect(p, TK_LBRACE, "{")) { ast_free_expr(br.cond); break; }
        adv(p);
        body = parse_block(p);
        if (!body) { ast_free_expr(br.cond); break; }
        br.body = body;
        tmp = (IfBranch *)realloc(v, (size_t)(n+1)*sizeof(IfBranch));
        if (!tmp) { ast_free_expr(br.cond); ast_free_block(br.body); break; }
        v = tmp; v[n++] = br;
        if (!is_id(pk(p,0), "eli")) break;
        adv(p);
    }
    if (n == 0) { free(s); return NULL; }
    if (is_id(pk(p,0), "els")) {
        adv(p);
        if (!expect(p, TK_LBRACE, "{")) { free(v); free(s); return NULL; }
        adv(p);
        s->u.cond.els = parse_block(p);
    }
    s->u.cond.br = v;
    s->u.cond.n = n;
    return s;
}

static Stmt *parse_exec(Parser *p)
{
    Stmt *s = ast_stmt(ST_EXEC, pk(p,0)->line);
    Token *name;
    adv(p);                                    /* "exec" */
    if (!expect_glued(p, TK_LBRACKET, "[")) { free(s); return NULL; }
    adv(p);
    name = expect(p, TK_ID, "имя контейнера");
    if (!name) { free(s); return NULL; }
    s->u.exec.name = ast_strdup(name->text);
    adv(p);
    if (!expect(p, TK_RBRACKET, "]")) { free(s); return NULL; }
    adv(p);
    return s;
}

static Stmt *parse_named(Parser *p, StmtKind kind, const char *prefix)
{
    const char *suffix;
    Stmt *s;
    char *name;
    if (!cmd(p, prefix, &suffix)) return NULL;
    s = ast_stmt(kind, pk(p,0)->line);
    /* kon_X хранится под полным именем: exec[kon_X] ссылается на него. */
    name = ast_strdup(kind == ST_KON ? pk(p,0)->text : suffix);
    adv(p);
    if (!expect_glued(p, kind == ST_DEF ? TK_LBRACKET : TK_LBRACE,
                      kind == ST_DEF ? "[" : "{")) {
        free(name); free(s); return NULL;
    }
    adv(p);
    if (kind == ST_DEF) {                      /* параметры [a,b] */
        char **pp = NULL;
        int np = 0;
        while (pk(p,0)->kind != TK_RBRACKET && pk(p,0)->kind != TK_EOF) {
            Token *pt = pk(p,0);
            char **tmp;
            if (pt->kind != TK_ID) {
                sprintf(p->err, "%ld: параметром функции может быть только имя", pt->line);
                free(pp); free(name); free(s); return NULL;
            }
            tmp = (char **)realloc(pp, (size_t)(np+1)*sizeof(char*));
            if (!tmp) { free(pp); free(name); free(s); return NULL; }
            pp = tmp; pp[np++] = ast_strdup(pt->text);
            adv(p);
            if (pk(p,0)->kind == TK_COMMA) adv(p);
        }
        if (!expect(p, TK_RBRACKET, "]")) {
            int j; for (j = 0; j < np; j++) free(pp[j]); free(pp);
            free(name); free(s); return NULL;
        }
        adv(p);
        if (!expect(p, TK_LBRACE, "{")) {
            int j; for (j = 0; j < np; j++) free(pp[j]); free(pp);
            free(name); free(s); return NULL;
        }
        adv(p);
        s->u.named.params = pp;
        s->u.named.nparams = np;
    }
    s->u.named.name = name;
    s->u.named.body = parse_block(p);
    if (!s->u.named.body) { free(name); free(s); return NULL; }
    return s;
}

/* Список выражений до '}' -- ({...}), аргументы файлов/экрана/сенсора. */
static Expr **parse_arg_list(Parser *p, int *outn)
{
    Expr **v = NULL;
    int n = 0;
    *outn = 0;
    if (pk(p,0)->kind != TK_RBRACE) {
        for (;;) {
            Expr *a = parse_concat(p);
            Expr **tmp;
            if (!a) { int j; for (j = 0; j < n; j++) ast_free_expr(v[j]); free(v); return NULL; }
            tmp = (Expr **)realloc(v, (size_t)(n+1)*sizeof(Expr*));
            if (!tmp) { ast_free_expr(a); return NULL; }
            v = tmp; v[n++] = a;
            if (pk(p,0)->kind != TK_COMMA) break;
            adv(p);
        }
    }
    if (!expect(p, TK_RBRACE, "}")) {
        int j; for (j = 0; j < n; j++) ast_free_expr(v[j]); free(v); return NULL;
    }
    adv(p);
    *outn = n;
    return v;
}

static Stmt *parse_random(Parser *p)
{
    const char *suffix;
    Stmt *s;
    char *name;
    int n = 0;
    if (!cmd(p, "random", &suffix)) return NULL;
    s = ast_stmt(ST_RANDOM, pk(p,0)->line);
    name = (char *)malloc(strlen(suffix) + 6);
    if (name) { strcpy(name, "rand_"); strcat(name, suffix); }
    adv(p);
    if (!expect_glued(p, TK_LBRACE, "{")) { free(name); free(s); return NULL; }
    adv(p);
    s->u.rnd.items = parse_arg_list(p, &n);
    if (!s->u.rnd.items) { free(name); free(s); return NULL; }
    s->u.rnd.n = n;
    s->u.rnd.name = name;
    return s;
}

static Stmt *parse_file(Parser *p, int op)
{
    Stmt *s = ast_stmt(ST_FILE, pk(p,0)->line);
    int n = 0;
    Expr **v;
    s->u.file.op = op;
    adv(p);
    if (!expect_glued(p, TK_LBRACE, "{")) { free(s); return NULL; }
    adv(p);
    v = parse_arg_list(p, &n);
    if (!v) { free(s); return NULL; }
    s->u.file.a = n > 0 ? v[0] : NULL;
    s->u.file.b = n > 1 ? v[1] : NULL;
    { int i; for (i = 2; i < n; i++) ast_free_expr(v[i]); }
    free(v);
    if (n < 1) { free(s); return NULL; }
    return s;
}

static Stmt *parse_io(Parser *p, StmtKind kind, int op)
{
    Stmt *s = ast_stmt(kind, pk(p,0)->line);
    int n = 0;
    s->u.io.op = op;
    if (pk(p,0)->kind == TK_LBRACE) {
        adv(p);
        s->u.io.args = parse_arg_list(p, &n);
        if (!s->u.io.args) { free(s); return NULL; }
        s->u.io.n = n;
    } else {
        adv(p);                                /* scr_cur, scr_size, touch_down */
    }
    return s;
}

static Stmt *parse_try(Parser *p)
{
    Stmt *s = ast_stmt(ST_TRY, pk(p,0)->line);
    adv(p);                                    /* try */
    if (!expect_glued(p, TK_LBRACE, "{")) { free(s); return NULL; }
    adv(p);
    s->u.tryc.body = parse_block(p);
    if (!s->u.tryc.body) { free(s); return NULL; }
    if (!is_id(pk(p,0), "catch")) {
        sprintf(p->err, "%ld: ожидается catch", pk(p,0)->line);
        ast_free_block(s->u.tryc.body); free(s); return NULL;
    }
    adv(p);
    if (pk(p,0)->kind == TK_LBRACKET) {
        Token *name;
        adv(p);
        name = expect(p, TK_ID, "имя контейнера");
        if (!name) { ast_free_block(s->u.tryc.body); free(s); return NULL; }
        s->u.tryc.handler = ast_strdup(name->text);
        adv(p);
        if (!expect(p, TK_RBRACKET, "]")) {
            free(s->u.tryc.handler); ast_free_block(s->u.tryc.body); free(s); return NULL;
        }
        adv(p);
    } else if (pk(p,0)->kind == TK_LBRACE) {
        adv(p);
        s->u.tryc.catchblk = parse_block(p);
        if (!s->u.tryc.catchblk) { ast_free_block(s->u.tryc.body); free(s); return NULL; }
    } else {
        sprintf(p->err, "%ld: catch требует [kon_X] или {блок}", pk(p,0)->line);
        ast_free_block(s->u.tryc.body); free(s); return NULL;
    }
    return s;
}

static Stmt *parse_stmt(Parser *p)
{
    Token *t = pk(p,0);
    if (t->kind == TK_EOF || t->kind == TK_RBRACE) return NULL;
    if (t->kind != TK_ID) {
        sprintf(p->err, "%ld: ожидается команда, найдено %s", t->line, describe(t));
        return NULL;
    }
    if (is_id(t, "print")) return parse_print(p);
    if (strncmp(t->text, "input", 5) == 0 &&
        (t->text[5] == '_' || t->text[5] == 0)) return parse_input(p);
    if (is_id(t, "set")) return parse_set(p);
    if (is_id(t, "if")) return parse_if(p);
    if (is_id(t, "cycle") ||
        (strncmp(t->text, "cycle", 5) == 0 && digits_only(t->text + 5)))
        return parse_cycle(p);
    if (is_id(t, "sc")) { adv(p); return ast_stmt(ST_BREAK, t->line); }
    if (is_id(t, "clear")) { adv(p); return ast_stmt(ST_CLEAR, t->line); }
    if (is_id(t, "ret")) {
        Stmt *s = ast_stmt(ST_RET, t->line);
        adv(p);
        if (pk(p,0)->kind == TK_LBRACKET) {
            adv(p);
            s->u.ret.value = parse_bracket_inner(p);
            if (!s->u.ret.value || !expect(p, TK_RBRACKET, "]")) {
                ast_free_block(NULL); free(s); return NULL;
            }
            adv(p);
        }
        return s;
    }
    if (is_id(t, "program") && pk(p,1)->kind == TK_MINUS && is_id(pk(p,2), "exit")) {
        adv(p); adv(p); adv(p);
        return ast_stmt(ST_EXIT, t->line);
    }
    if (strncmp(t->text, "random", 6) == 0 &&
        (t->text[6] == '_' || t->text[6] == 0)) return parse_random(p);
    if (strncmp(t->text, "key", 3) == 0 &&
        (t->text[3] == '_' || t->text[3] == 0)) {
        Stmt *s = ast_stmt(ST_KEY, t->line);
        char *nm = (char *)malloc(strlen(t->text) + 2);
        strcpy(nm, "key_");
        strcat(nm, t->text[3] == '_' ? t->text + 4 : t->text + 3);
        s->u.key.name = nm;
        adv(p);
        return s;
    }
    if (strncmp(t->text, "time", 4) == 0 && digits_only(t->text + 4)) {
        Stmt *s = ast_stmt(ST_TIME, t->line);
        Expr *e = ast_expr(EX_INT, t->line);
        e->u.i = atol(t->text + 4);
        s->u.expr = e;
        adv(p);
        return s;
    }
    if (is_id(t, "openFile")) return parse_file(p, KF_OPEN);
    if (is_id(t, "writeFile")) return parse_file(p, KF_WRITE);
    if (is_id(t, "appendFile")) return parse_file(p, KF_APPEND);
    if (is_id(t, "searchFile")) return parse_file(p, KF_SEARCH);
    if (is_id(t, "delFile")) return parse_file(p, KF_DEL);
    if (is_id(t, "scr_create")) return parse_io(p, ST_SCREEN, KS_CREATE);
    if (is_id(t, "scr_size")) return parse_io(p, ST_SCREEN, KS_SIZE);
    if (is_id(t, "scr_bg")) return parse_io(p, ST_SCREEN, KS_BG);
    if (is_id(t, "scr_PaintOut")) return parse_io(p, ST_SCREEN, KS_PAINT);
    if (is_id(t, "scr_beep")) return parse_io(p, ST_SCREEN, KS_BEEP);
    if (is_id(t, "scr_cur")) return parse_io(p, ST_TOUCH, KT_CUR);
    if (is_id(t, "touch_down")) return parse_io(p, ST_TOUCH, KT_DOWN);
    if (is_id(t, "touch_button")) return parse_io(p, ST_TOUCH, KT_BUTTON);
    if (is_id(t, "touch_hold")) return parse_io(p, ST_TOUCH, KT_HOLD);
    if (is_id(t, "touch_swipe")) return parse_io(p, ST_TOUCH, KT_SWIPE);
    if (is_id(t, "touch_idle")) return parse_io(p, ST_TOUCH, KT_IDLE);
    if (is_id(t, "try")) return parse_try(p);
    if (is_id(t, "exec")) return parse_exec(p);
    if (is_id(t, "start") && pk(p,1)->kind == TK_COLON) {
        adv(p); adv(p);
        return parse_exec(p);
    }
    {
        Stmt *s = parse_named(p, ST_DEF, "def");
        if (s) return s;
    }
    {
        Stmt *s = parse_named(p, ST_KON, "kon");
        if (s) return s;
    }
    if (!p->err[0])
        sprintf(p->err, "%ld: неизвестная команда '%s'", t->line, t->text);
    return NULL;
}

static Block *parse_block(Parser *p)
{
    Block *b = ast_block();
    for (;;) {
        Stmt *s;
        Token *t = pk(p,0);
        if (t->kind == TK_RBRACE) { adv(p); return b; }
        if (t->kind == TK_EOF) {
            sprintf(p->err, "%ld: незакрытый '{'", t->line);
            ast_free_block(b); return NULL;
        }
        s = parse_stmt(p);
        if (!s) { ast_free_block(b); return NULL; }
        ast_block_add(b, s);
    }
}

int parse_program(const char *src, size_t len, Program **out,
                  char *err, size_t errsz)
{
    Parser p;
    Program *prog;
    Token *tokens = NULL;
    int ntok = 0;

    if (err && errsz) err[0] = 0;
    memset(&p, 0, sizeof(p));
    if (!lex_tokenize(src, len, &tokens, &ntok,
                      err && errsz ? err : p.err,
                      err && errsz ? errsz : sizeof(p.err))) {
        if (!err) strcpy(p.err, "ошибка лексера");
        return 0;
    }
    p.t = tokens; p.n = ntok; p.i = 0;
    prog = (Program *)calloc(1, sizeof(Program));
    if (!prog) { lex_free(tokens, ntok); return 0; }
    prog->body = ast_block();
    while (pk(&p,0)->kind != TK_EOF) {
        Stmt *s = parse_stmt(&p);
        if (!s) {
            if (err && errsz) {
                if (!err[0]) sprintf(err, "%s", p.err[0] ? p.err : "ошибка разбора");
            }
            ast_free_program(prog);
            lex_free(tokens, ntok);
            return 0;
        }
        ast_block_add(prog->body, s);
    }
    lex_free(tokens, ntok);
    *out = prog;
    return 1;
}
