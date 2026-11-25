#ifndef KON_LEXER_H
#define KON_LEXER_H

#include <stddef.h>

typedef enum {
    TK_EOF = 0, TK_ID, TK_STRING, TK_NUMBER,
    TK_LBRACE, TK_RBRACE, TK_LBRACKET, TK_RBRACKET,
    TK_COMMA, TK_ASSIGN, TK_EQ, TK_NE, TK_LT, TK_GT, TK_LE, TK_GE,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_DOT, TK_COLON
} TokKind;

typedef struct {
    TokKind kind;
    char *text;          /* UTF-8, для ID / STRING / NUMBER */
    long line;
    size_t start, end;   /* смещения в исходнике — нужны для проверки «слитно» */
} Token;

/* Разбивает текст на токены. Возвращает 0 при успехе. */
int lex_tokenize(const char *src, size_t len, Token **out, int *outn,
                 char *err, size_t errsz);
void lex_free(Token *t, int n);
const char *tok_name(TokKind k);

#endif
