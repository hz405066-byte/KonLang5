#ifndef KON_CP866_H
#define KON_CP866_H

#include <stddef.h>

/* Читает .kn: автоопределение UTF-8 / CP866, возвращает malloc-строку в UTF-8. */
char *kon_read_text(const char *path, size_t *len, char *err, size_t errsz);

/* Обратная конверсия: UTF-8 -> CP866 (для строк в генерируемом .COM/.EXE). */
char *kon_to_cp866(const char *utf8);

/* Имя распознанной кодировки последнего kon_read_text(). */
const char *kon_source_encoding(void);

#endif
