#!/usr/bin/env bash
# Corre la matriz completa de la fase 0. Ajusta NGL a tu GPU (8 GB: usa -1 para todo).
set -u
cd "$(dirname "$0")/.."
PY=.venv/Scripts/python
run() { echo "=== $*"; $PY eval/run_eval.py "$@" || echo "FALLO: $*"; }
# idioma de la pregunta: es
run --model vela3-fast-ml  --lang es
run --model onejev1.6-0.8b --lang es --ngl ${NGL_SMALL:-99}
run --model qwen3.5-0.8b   --lang es --ngl ${NGL_SMALL:-99}
run --model onejev1.6-4b   --lang es --ngl ${NGL_4B:-18}
run --model qwen3.5-4b     --lang es --ngl ${NGL_4B:-18}
run --model onejev1.6-9b   --lang es --ngl ${NGL_9B:-6}
run --model qwen3.5-9b     --lang es --ngl ${NGL_9B:-6}
# idioma de la pregunta: en (mismos textos en español)
run --model vela3-fast-ml  --lang en
run --model onejev1.6-0.8b --lang en --ngl ${NGL_SMALL:-99}
run --model onejev1.6-4b   --lang en --ngl ${NGL_4B:-18}
run --model onejev1.6-9b   --lang en --ngl ${NGL_9B:-6}
echo "=== FIN"
