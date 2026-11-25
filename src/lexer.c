/*
 * lexer.c -- токенайзер KonLang.
 * Источник уже в UTF-8. Байты >= 0x80 считаются «буквами» — этим
 * покрываются и кириллические идентификаторы (kon_ошибка), и строки.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"

#define MAX_TOK 65536

typedef struct {
    const char *s;
    size_t n, p;
    long line;
    Token *v;
    int cnt;
    char *err;
    size_t errsz;
} Scanner;

static char *xstrndup(const char *s, size_t n)
{
    char *p = (char *)malloc(n + 1);
    if (p) { memcpy(p, s, n); p[n] = 0; }
    return p;
}

static Token *push(Scanner *sc, TokKind k, size_t start)
{
    Token *t;
    if (sc->cnt >= MAX_TOK) return NULL;
    t = &sc->v[sc->cnt++];
    t->kind = k;
    t->text = NULL;
    t->line = sc->line;
    t->start = start;
    t->end = sc->p;
    return t;
}

static int fail(Scanner *sc, const char *msg)
{
    sprintf(sc->err, "%ld: %s", sc->line, msg);
    return 0;
}

static int is_id_start(unsigned c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80;
}

static int is_id_cont(unsigned c)
{
    return is_id_start(c) || (c >= '0' && c <= '9');
}

/* Пропуск пробелов и комментариев вида * текст * (звёздочка перед пробелом). */
static void skip_space(Scanner *sc)
{
    for (;;) {
        unsigned c = (unsigned char)sc->s[sc->p];
        if (c == ' ' || c == '\t' || c == '\r') { sc->p++; continue; }
        if (c == '\n') { sc->p++; sc->line++; continue; }
        if (c == '*' && sc->p + 1 < sc->n &&
            (sc->s[sc->p+1] == ' ' || sc->s[sc->p+1] == '\t' || sc->s[sc->p+1] == '\n')) {
            sc->p++;
            while (sc->p < sc->n && sc->s[sc->p] != '*') {
                if (sc->s[sc->p] == '\n') sc->line++;
                sc->p++;
            }
            if (sc->p < sc->n) sc->p++;
            continue;
        }
        break;
    }
}

static int scan_string(Scanner *sc)
{
    size_t start = sc->p, outn = 0;
    char *buf;
    sc->p++;                       /* открывающая кавычка */
    buf = (char *)malloc(sc->n - start + 1);
    if (!buf) return fail(sc, "нет памяти");
    while (sc->p < sc->n) {
        unsigned char c = (unsigned char)sc->s[sc->p];
        if (c == '"') { sc->p++; break; }
        if (c == '\n') sc->line++;
        if (c == '\\' && sc->p + 1 < sc->n) {
            char d = sc->s[sc->p+1];
            if (d == 'n') buf[outn++] = '\n';
            else if (d == 't') buf[outn++] = '\t';
            else if (d == '\\') buf[outn++] = '\\';
            else if (d == '"') buf[outn++] = '"';
            else { buf[outn++] = '\\'; buf[outn++] = d; }
            sc->p += 2;
            continue;
        }
        buf[outn++] = (char)c;
        sc->p++;
    }
    if (sc->p > sc->n || sc->s[sc->p-1] != '"') {
        free(buf);
        return fail(sc, "незакрытая строка");
    }
    buf[outn] = 0;
    push(sc, TK_STRING, start)->text = buf;
    return 1;
}

int lex_tokenize(const char *src, size_t len, Token **out, int *outn,
                 char *err, size_t errsz)
{
    Scanner sc;
    int ok = 1;

    if (err && errsz) err[0] = 0;
    memset(&sc, 0, sizeof(sc));
    sc.s = src; sc.n = len; sc.line = 1; sc.err = err; sc.errsz = errsz;
    sc.v = (Token *)calloc(MAX_TOK, sizeof(Token));
    if (!sc.v) { if (err) sprintf(err, "нет памяти"); return 0; }

    while (ok && sc.p < sc.n) {
        size_t start;
        unsigned c;
        skip_space(&sc);
        if (sc.p >= sc.n) break;
        start = sc.p;
        c = (unsigned char)sc.s[sc.p];

        if (c == '"') { ok = scan_string(&sc); continue; }
        if (is_id_start(c)) {
            while (sc.p < sc.n && is_id_cont((unsigned char)sc.s[sc.p])) sc.p++;
            push(&sc, TK_ID, start)->text = xstrndup(sc.s + start, sc.p - start);
            continue;
        }
        if (c >= '0' && c <= '9') {
            int dot = 0;
            while (sc.p < sc.n) {
                unsigned d = (unsigned char)sc.s[sc.p];
                if (d >= '0' && d <= '9') { sc.p++; continue; }
                if (d == '.' && !dot && sc.p + 1 < sc.n &&
                    sc.s[sc.p+1] >= '0' && sc.s[sc.p+1] <= '9') {
                    dot = 1; sc.p++; continue;
                }
                break;
            }
            push(&sc, TK_NUMBER, start)->text = xstrndup(sc.s + start, sc.p - start);
            continue;
        }

        sc.p++;
        switch (c) {
        case '{': push(&sc, TK_LBRACE, start); break;
        case '}': push(&sc, TK_RBRACE, start); break;
        case '[': push(&sc, TK_LBRACKET, start); break;
        case ']': push(&sc, TK_RBRACKET, start); break;
        case ',': push(&sc, TK_COMMA, start); break;
        case '.': push(&sc, TK_DOT, start); break;
        case ':': push(&sc, TK_COLON, start); break;
        case '+': push(&sc, TK_PLUS, start); break;
        case '-': push(&sc, TK_MINUS, start); break;
        case '*': push(&sc, TK_STAR, start); break;
        case '/': push(&sc, TK_SLASH, start); break;
        case '=':
            if (sc.p < sc.n && sc.s[sc.p] == '=') { sc.p++; push(&sc, TK_EQ, start); }
            else push(&sc, TK_ASSIGN, start);
            break;
        case '!':
            if (sc.p < sc.n && sc.s[sc.p] == '=') { sc.p++; push(&sc, TK_NE, start); }
            else ok = fail(&sc, "одиночный '!' не поддерживается");
            break;
        case '>':
            if (sc.p < sc.n && sc.s[sc.p] == '=') { sc.p++; push(&sc, TK_GE, start); }
            else push(&sc, TK_GT, start);
            break;
        case '<':
            if (sc.p < sc.n && sc.s[sc.p] == '=') { sc.p++; push(&sc, TK_LE, start); }
            else push(&sc, TK_LT, start);
            break;
        default:
            ok = fail(&sc, "неизвестный символ");
            break;
        }
    }
    push(&sc, TK_EOF, sc.n);

    if (!ok) { lex_free(sc.v, sc.cnt); return 0; }
    *out = sc.v;
    *outn = sc.cnt;
    return 1;
}

void lex_free(Token *t, int n)
{
    int i;
    if (!t) return;
    for (i = 0; i < n; i++) free(t[i].text);
    free(t);
}

const char *tok_name(TokKind k)
{
    switch (k) {
    case TK_EOF: return "конец файла";
    case TK_ID: return "имя";
    case TK_STRING: return "строка";
    case TK_NUMBER: return "число";
    case TK_LBRACE: return "{";
    case TK_RBRACE: return "}";
    case TK_LBRACKET: return "[";
    case TK_RBRACKET: return "]";
    case TK_COMMA: return ",";
    case TK_ASSIGN: return "=";
    case TK_EQ: return "==";
    case TK_NE: return "!=";
    case TK_LT: return "<";
    case TK_GT: return ">";
    case TK_LE: return "<=";
    case TK_GE: return ">=";
    case TK_PLUS: return "+";
    case TK_MINUS: return "-";
    case TK_STAR: return "*";
    case TK_SLASH: return "/";
    case TK_DOT: return ".";
    case TK_COLON: return ":";
    }
    return "?";
}
