@echo off
rem Сборка konlang в DOS (Turbo C 2.01 / Borland C++ 3.1).
rem Основная поддерживаемая сборка -- `make` на Linux (см. README.md).
rem Пути к компилятору и NASM подставьте под свою систему.
set TC=C:\TC
%TC%\BIN\TCC -O -I%TC%\INCLUDE -ISRC -ekonlang.exe ^
  SRC\MAIN.C SRC\CP866.C SRC\LEXER.C SRC\AST.C SRC\PARSER.C ^
  SRC\CODEGEN.C SRC\INTERP.C
if errorlevel 1 goto fail
echo OK: konlang.exe
goto end
:fail
echo BUILD FAILED
:end
