#!/usr/bin/env bash
# Testes para count_words.c
set -e
cd "$(dirname "$0")"
make -s

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
printf 'one two three four five\n' >"$tmp"

echo "=== test 1: sem argumentos ==="
echo "esperado: stderr com linha de uso (Uso: ... <ficheiro>), código de saída 1"
set +e
out=$(./count_words 2>&1)
ec=$?
set -e
echo "obtido:   código de saída $ec"
echo "          (stderr reunido com stdout para inspeção:)"
printf '%s\n' "$out" | sed 's/^/          | /'
test "$ec" -eq 1
echo "$out" | grep -q 'Uso:'
echo "resultado: OK"
echo

echo "=== test 2: demasiados argumentos ==="
echo "esperado: stderr com mensagem de uso, código de saída 1"
set +e
out=$(./count_words "$tmp" extra 2>&1)
ec=$?
set -e
echo "obtido:   código de saída $ec"
printf '%s\n' "$out" | sed 's/^/          | /'
test "$ec" -eq 1
echo "$out" | grep -q 'Uso:'
echo "resultado: OK"
echo

echo "=== test 3: ficheiro válido ==="
echo "esperado: stdout só com 5 (e newline), código de saída 0"
set +e
out=$(./count_words "$tmp" 2>&1)
ec=$?
set -e
echo "obtido:   código de saída $ec"
printf '%s\n' "$out" | sed 's/^/          | /'
test "$ec" -eq 0
test "$out" = "5"
echo "resultado: OK"
echo

echo "ex2 testcase.sh: todos os testes ocorreram como esperado"
