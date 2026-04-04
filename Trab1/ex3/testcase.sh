#!/usr/bin/env bash
# Testes para vector-seq.c e vector-seq-processes.c
set -e
cd "$(dirname "$0")"
make -s

DIM=400
WORKERS=4

echo "=== test 1: vector-seq com dimensão 0 ==="
echo "esperado: stderr com mensagem de uso (Uso: ... <dimensao_vetor>), código de saída 1"
set +e
out=$(./vector-seq 0 2>&1)
ec=$?
set -e
echo "obtido:   código de saída $ec"
echo "          (stderr reunido com stdout para inspeção:)"
printf '%s\n' "$out" | sed 's/^/          | /'
test "$ec" -eq 1
echo "$out" | grep -q 'Uso:'
echo "resultado: OK"
echo

echo "=== test 2: vector-seq-processes sem argumentos ==="
echo "esperado: stderr com mensagem de uso (dimensão e num_processos), código de saída 1"
set +e
out=$(./vector-seq-processes 2>&1)
ec=$?
set -e
echo "obtido:   código de saída $ec"
printf '%s\n' "$out" | sed 's/^/          | /'
test "$ec" -eq 1
echo "$out" | grep -q 'Uso:'
echo "resultado: OK"
echo

echo "=== test 3: vector-seq-processes só com dimensão (falta num_processos) ==="
echo "esperado: stderr com mensagem de uso, código de saída 1"
set +e
out=$(./vector-seq-processes "$DIM" 2>&1)
ec=$?
set -e
echo "obtido:   código de saída $ec"
printf '%s\n' "$out" | sed 's/^/          | /'
test "$ec" -eq 1
echo "$out" | grep -q 'Uso:'
echo "resultado: OK"
echo

echo "=== test 4: vector-seq-processes com dimensão 0 ==="
echo "esperado: stderr com mensagem de uso, código de saída 1"
set +e
out=$(./vector-seq-processes 0 "$WORKERS" 2>&1)
ec=$?
set -e
echo "obtido:   código de saída $ec"
printf '%s\n' "$out" | sed 's/^/          | /'
test "$ec" -eq 1
echo "$out" | grep -q 'Uso:'
echo "resultado: OK"
echo

echo "=== test 5: vector-seq-processes com num_processos 0 ==="
echo "esperado: stderr com mensagem de uso, código de saída 1"
set +e
out=$(./vector-seq-processes "$DIM" 0 2>&1)
ec=$?
set -e
echo "obtido:   código de saída $ec"
printf '%s\n' "$out" | sed 's/^/          | /'
test "$ec" -eq 1
echo "$out" | grep -q 'Uso:'
echo "resultado: OK"
echo

echo "=== test 6: vector-seq e vector-seq-processes (mesmos smaller / bigger / sum) ==="
echo "esperado: ambos código 0; linhas 'smaller is', 'bigger  is', 'The sum is' idênticas"
vs=$(mktemp)
vp=$(mktemp)
trap 'rm -f "$vs" "$vp"' EXIT
set +e
out_seq=$(./vector-seq "$DIM" 2>&1)
ec_seq=$?
out_par=$(./vector-seq-processes "$DIM" "$WORKERS" 2>&1)
ec_par=$?
set -e
echo "obtido:   vector-seq código de saída $ec_seq"
printf '%s\n' "$out_seq" | grep -E '^(smaller is|bigger  is|The sum is) ' | sed 's/^/          | seq: /'
echo "obtido:   vector-seq-processes código de saída $ec_par"
printf '%s\n' "$out_par" | grep -E '^(smaller is|bigger  is|The sum is) ' | sed 's/^/          | par: /'
test "$ec_seq" -eq 0
test "$ec_par" -eq 0
printf '%s\n' "$out_seq" | grep -E '^(smaller is|bigger  is|The sum is) ' >"$vs"
printf '%s\n' "$out_par" | grep -E '^(smaller is|bigger  is|The sum is) ' >"$vp"
diff -q "$vs" "$vp" >/dev/null
echo "resultado: OK (diff das três linhas numéricas sem diferenças)"
echo

echo "ex3 testcase.sh: todos os testes ocorreram como esperado"
