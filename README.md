# KonLang v5 — интерпретатор и компилятор для MS-DOS

KonLang — компонентно-ориентированный язык для консольных TUI/CUI-программ.
Этот проект содержит:

* `konlang` — хост-утилита (Linux, C89): лексер, парсер, интерпретатор и
  генератор ассемблера;
* `runtime/rt_console.inc` — библиотека подпрограмм на NASM (INT 10h/16h/21h);
* готовые `.COM`/`.EXE` для проверки в DOSBox.

## 1. Сборка компилятора

```sh
make                 # gcc + NASM + DOSBox уже должны быть в системе
make check           # быстрая проверка парсера на examples/hello.kn
make clean
```

`BUILD.BAT` — заготовка для сборки компилятора в DOS (Turbo C 2.01 /
Borland C++ 3.1) при наличии соответствующих заголовков; основная сборка
и тестирование ведутся на хосте.

Требуемые инструменты: `gcc`, `make`, `nasm`, `dosbox` (для тестов).

## 2. Использование

```
konlang check program.kn              только разбор, печать AST
konlang run   program.kn              интерпретация без сборки (полная семантика)
konlang build program.kn              program.COM (если влезает) или program.EXE
konlang build program.kn --exe        принудительно MZ .EXE
```

Имя выходного файла — имя исходника в верхнем регистре: `hello.kn → HELLO.COM`.

### Кодировки

Исходник может быть в UTF-8, CP866 или ASCII — определяется автоматически.
Внутри компилятора строки хранятся в UTF-8, в сгенерированные бинарники
кладутся байтами **CP866** (см. таблицу в `ascii.pdf`, стр. 4: `0x80–0x9F`
заглавные, `0xA0–0xBE` строчные, `0xC0–0xDF` псевдографика, `Ё=0xF0`,
`ё=0xF1`). Поэтому кириллица и рамки из примеров корректно отображаются в DOS.

## 3. Что реализовано

| Подсистема | `run` | `.COM` / `.EXE` |
|---|---|---|
| `print`, координаты, склейка, `*` строки | да | да |
| `input_X` (в т.ч. `int`/`float`, лимит) | да | да (проверка типа — в `run`) |
| `set`, `math` (int, float=деление), `len`, списки, индексы | да | int + деление float; списки и строковые индексы — да (простые случаи) |
| `cycle`, `cycleN`, `cycle[N]`, `sc`, `if/eli/els` | да | да (вложенные циклы с сохранением CX) |
| `kon_X`, `exec` | да | да (inline подстановка тела); `def_X`/`ret` — только в `run` |
| `clear`, `key_X`, `random_X`, `timeX` | да | `clear` — да; остальные — в `run` |
| `openFile`, `writeFile`, `appendFile`, `searchFile`, `delFile` | да | только в `run` |
| `try/catch`, `[err]` | да | только в `run` |
| `scr_create/size/bg/PaintOut/beep` | частично | `scr_create` — координаты print; остальное — заглушки |
| `scr_cur`, `touch_*` | заглушки ([touch]=0) | заглушки |

`.COM`: `ORG 100h`, `DS=CS=ES=SS`, стек в конце сегмента, лимит 65 280 байт.
`.EXE`: собственный MZ-заголовок, отдельные секции CODE/DATA/STACK, стек 4 КБ,
таблица релокаций пуста (сегменты считаются через `CS + delta`). Лимит 640 КБ.

Если program.COM не влезает в 64 КБ, компилятор печатает предупреждение и
автоматически создаёт program.EXE.

## 4. Примеры

* `examples/hello.kn` — Hello, World;
* `examples/calc.kn` — калькулятор (`input` + `math`);
* `examples/menu.kn` — меню (`cycle` + `if` + `kon_X`);
* `examples/file.kn` — запись/чтение/дозапись файла;
* `examples/caesar.kn` — шифр Цезаря (проверен в `run`; в `.COM` — только
  упрощённые варианты — см. ограничения).

```sh
./konlang build examples/hello.kn          # HELLO.COM
./konlang build examples/calc.kn           # CALC.COM
./konlang run   examples/caesar.kn         # интерактивно
```

## 5. Тесты в DOSBox

```sh
./konlang build examples/hello.kn
timeout 60 dosbox -noconsole \
  -c 'mount c "/путь/к/KonLang"' -c 'c:' \
  -c 'hello.com > out.txt' -c 'exit'
iconv -f CP866 -t UTF-8 OUT.TXT
```

`tests/TEST.BAT` — тот же checks внутри DOS (FC сравнение с эталоном).

## 6. Ограничения

* Режим `run` — эталонная семантика; скомпилированные программы покрывают
  ядро языка (таблица выше). Команды, отмеченные «только в run», при сборке
  дают понятную ошибку, а не неверный код.
* Полный `caesar.kn` с 33-буквенным алфавитом в `.COM` может зацикливаться —
  используйте `konlang run`. Простые шифры (3–4 буквы) компилируются нормально.
* Сенсорные функции на DOS — заглушки: аппаратных touch-драйверов в DOS нет.
  Их семантика полностью реализована в `run`.
* `double` реализован как 32-битный `float` (по согласованию).
* Строки в `.COM` — CP866, один байт на символ; в `run` строки UTF-8 и
  индексация считает символы, а не байты.

## 7. Структура

```
src/cp866.*    кодировка      src/lexer.*   токены
src/parser.*   рекурсивный спуск, AST
src/ast.*      узлы, печать AST
src/interp.*   интерпретатор (run)
src/codegen.*  генератор .asm, NASM, MZ-заголовок
src/main.c     CLI
runtime/rt_console.inc
examples/  tests/
```
