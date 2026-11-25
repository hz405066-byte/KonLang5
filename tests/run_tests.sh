#!/bin/sh
# Хост-тесты: check/run для всех пяти примеров + сборка .COM и прогон в DOSBox.
set -e
cd "$(dirname "$0")/.."
for f in hello calc menu file caesar; do
  ./konlang check "examples/$f.kn" > /dev/null
  echo "check $f: OK"
done
printf '10\n4\n' | ./konlang run examples/calc.kn > /dev/null
printf '3\n' | ./konlang run examples/caesar.kn > /dev/null
./konlang run examples/file.kn > /dev/null
./konlang build examples/hello.kn > /dev/null
PROJ="$PWD"
timeout 60 dosbox -noconsole \
  -c "mount c \"$PROJ\"" -c 'c:' \
  -c 'hello.com > out.txt' -c 'exit' > /dev/null 2>&1 || true
if iconv -f CP866 -t UTF-8 OUT.TXT | grep -q 'Hello, World!'; then
  echo 'dosbox hello.com: OK'
else
  echo 'dosbox hello.com: FAIL'
  exit 1
fi
echo 'ALL TESTS PASSED'
