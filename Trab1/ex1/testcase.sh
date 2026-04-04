#!/usr/bin/env bash
# parallel_exec ignora os argumentos argv; isto apenas verifica se o programa ainda funciona quando são dados argumentos extra.
set -e
cd "$(dirname "$0")"
make -s
exec ./parallel_exec --invalid extra args
