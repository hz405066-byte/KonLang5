/*
 * cp866.c -- кодировочный слой.
 *
 * SQLite-правило простое: внутри компилятора всё в UTF-8, на входе/выходе
 * CP866. Таблица взята из приложенного ascii.pdf, стр. 4:
 *   80..9F = А..Я, A0..BE = а..я (BE = ╛), C0..DF = псевдографика,
 *   E0..EF = р..я, F0 = Ё, F1 = ё, F2..F7 = ЄєЇїЎў, F8..FE = °··√№¤■.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cp866.h"

/* Коды Unicode для байтов 0x80..0xFF. */
static const unsigned short cp866_map[128] = {
    0x0410,0x0411,0x0412,0x0413,0x0414,0x0415,0x0416,0x0417, /* 80-87 */
    0x0418,0x0419,0x041A,0x041B,0x041C,0x041D,0x041E,0x041F, /* 88-8F */
    0x0420,0x0421,0x0422,0x0423,0x0424,0x0425,0x0426,0x0427, /* 90-97 */
    0x0428,0x0429,0x042A,0x042B,0x042C,0x042D,0x042E,0x042F, /* 98-9F */
    0x0430,0x0431,0x0432,0x0433,0x0434,0x0435,0x0436,0x0437, /* A0-A7 */
    0x0438,0x0439,0x043A,0x043B,0x043C,0x043D,0x043E,0x043F, /* A8-AF */
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556, /* B0-B7 */
    0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510, /* B8-BF */
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F, /* C0-C7 */
    0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567, /* C8-CF */
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B, /* D0-D7 */
    0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580, /* D8-DF */
    0x0440,0x0441,0x0442,0x0443,0x0444,0x0445,0x0446,0x0447, /* E0-E7 */
    0x0448,0x0449,0x044A,0x044B,0x044C,0x044D,0x044E,0x044F, /* E8-EF */
    0x0401,0x0451,0x0404,0x0454,0x0407,0x0457,0x040E,0x045E, /* F0-F7 */
    0x00B0,0x00B7,0x00B7,0x221A,0x2116,0x00A4,0x25A0,0x00A0  /* F8-FF */
};

static const char *encoding_name = "ASCII";

const char *kon_source_encoding(void) { return encoding_name; }

/* Строгая проверка UTF-8 (включая отсечение неполных последовательностей). */
static int utf8_valid(const unsigned char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned c = s[i];
        int need, k;
        unsigned cp;
        if (c < 0x80) { i++; continue; }
        if (c >= 0xC2 && c <= 0xDF) { need = 1; cp = c & 0x1Fu; }
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; cp = c & 0x0Fu; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; cp = c & 0x07u; }
        else return 0;
        if (i + (size_t)need >= n) return 0;
        if (c == 0xE0 && s[i+1] < 0xA0) return 0;
        if (c == 0xED && s[i+1] >= 0xA0) return 0;
        if (c == 0xF0 && s[i+1] < 0x90) return 0;
        if (c == 0xF4 && s[i+1] > 0x8F) return 0;
        for (k = 1; k <= need; k++) {
            if ((s[i+k] & 0xC0) != 0x80) return 0;
            cp = (cp << 6) | (s[i+k] & 0x3Fu);
        }
        i += (size_t)need + 1;
    }
    return 1;
}

static void put_utf8(char **out, unsigned cp)
{
    char *p = *out;
    if (cp < 0x80) {
        *p++ = (char)cp;
    } else if (cp < 0x800) {
        *p++ = (char)(0xC0 | (cp >> 6));
        *p++ = (char)(0x80 | (cp & 0x3F));
    } else {
        *p++ = (char)(0xE0 | (cp >> 12));
        *p++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *p++ = (char)(0x80 | (cp & 0x3F));
    }
    *out = p;
}

char *kon_read_text(const char *path, size_t *len, char *err, size_t errsz)
{
    FILE *f;
    unsigned char *raw;
    size_t n, cap, i, j;
    char *dec;
    int has_high = 0, is_utf8;

    (void)errsz;
    f = fopen(path, "rb");
    if (!f) {
        sprintf(err, "не удалось открыть '%s'", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    raw = (unsigned char *)malloc(n + 1);
    if (!raw || fread(raw, 1, n, f) != n) {
        sprintf(err, "ошибка чтения '%s'", path);
        fclose(f); free(raw); return NULL;
    }
    fclose(f);
    raw[n] = 0;

    for (i = 0; i < n; i++) {
        if (raw[i] >= 0x80) { has_high = 1; break; }
    }
    /* Валидатор сам проверит ведущие байты: CP866-кириллица (0x80..0xBF)
       не может быть валидным UTF-8, а псевдографика 0xC0..0xDF почти всегда
       ломает последовательность. */
    is_utf8 = has_high && utf8_valid(raw, n);
    encoding_name = !has_high ? "ASCII" : (is_utf8 ? "UTF-8" : "CP866");

    cap = n * 3 + 1;               /* запас: CP866-байт -> до 3 байт UTF-8 */
    dec = (char *)malloc(cap);
    if (!dec) { free(raw); sprintf(err, "нет памяти"); return NULL; }
    j = 0;
    for (i = 0; i < n; i++) {
        char *p = dec + j;
        if (is_utf8 || raw[i] < 0x80) {
            /* Уже UTF-8 (или ASCII) — копируем байты как есть. */
            *p++ = (char)raw[i];
        } else {
            put_utf8(&p, cp866_map[raw[i] - 0x80]);
        }
        j = (size_t)(p - dec);
    }
    dec[j] = 0;
    free(raw);
    if (len) *len = j;
    return dec;
}

/* UTF-8 -> CP866. Символы вне таблицы заменяются на '?'. */
char *kon_to_cp866(const char *utf8)
{
    size_t n = strlen(utf8), i = 0, j = 0;
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    while (i < n) {
        unsigned char c = (unsigned char)utf8[i];
        unsigned cp = c;
        int k, need = 0;
        if (c >= 0xC2 && c <= 0xDF) { need = 1; cp = c & 0x1Fu; }
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; cp = c & 0x0Fu; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; cp = c & 0x07u; }
        for (k = 1; k <= need && i + (size_t)k < n; k++)
            cp = (cp << 6) | ((unsigned char)utf8[i+k] & 0x3Fu);
        i += (size_t)need + 1;

        if (cp < 0x80) {
            out[j++] = (char)cp;
        } else {
            int b = -1, t;
            for (t = 0; t < 128; t++) {
                if (cp866_map[t] == cp) { b = t + 0x80; break; }
            }
            out[j++] = b >= 0 ? (char)b : '?';
        }
    }
    out[j] = 0;
    return out;
}
