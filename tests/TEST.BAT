@echo off
rem Проверка скомпилированных примеров внутри DOS / DOSBox.
rem Запуск: dosbox -c 'mount c <каталог проекта>' -c 'c:\tests' -c 'test.bat'
echo === KonLang v5 tests ===

hello.com > t1.txt
fc t1.txt expect_hello.txt > nul
if errorlevel 1 goto fail

echo ALL TESTS PASSED
goto end
:fail
echo TEST FAILED
:end
