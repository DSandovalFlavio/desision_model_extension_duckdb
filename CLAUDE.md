# decide: guía para agentes

Extensión de **DuckDB 2.0.0** (C++17) que clasifica texto desde SQL con modelos de decisión **OneJev1.6** (0.8B / **4B por defecto** / 9B, GGUF, Apache‑2.0) sobre **llama.cpp b12521**, enlazado estáticamente. Una pasada por decisión: probabilidad = softmax de los logits de las letras A, B, C… del prompt `qev-labels-v2`.

- Uso y compilación: [README.md](README.md).
- Decisiones, mediciones y pendientes: [PLAN.md](PLAN.md).
- Evaluación: [eval/RESULTS.md](eval/RESULTS.md).

El usuario escribe en español; responde en español. El uso es **comercial**: solo modelos con licencia permisiva (StartLux y openjev son CC BY‑NC y están descartados).

## Mapa del código

| Archivo | Qué hace |
|---|---|
| `src/decide_extension.cpp` | Registro: ajustes `decide_*`, funciones SQL y `decide_state` (JSON con sangría 2 al estilo Python) |
| `src/decide_runtime.cpp` | Motor llama.cpp: carga del modelo, lotes multisecuencia (`llama_batch_ext` + `llama_process`), logits solo en el último token, deduplicación, caché LRU, estadísticas |
| `src/decide_prompt.cpp` | Prompt `qev-labels-v2` + plantilla de chat Qwen3.5 sin razonamiento. **Debe coincidir byte a byte** con `eval/run_eval.py` y con `llama-server /apply-template` |
| `src/decide_grammar.cpp` | Cláusula `DECIDE` (gramática PEG), port de altertable-ai/duckdb-decide_onnx (MIT) |
| `src/decide_store.cpp` | Descarga automática vía `httpfs`, SHA‑256 propio, archivo temporal + rename, sello `.verified` |
| `src/decide_models.cpp` | Registro de los 3 modelos: repo de HF, archivo, tamaño y sha256 fijados |
| `scripts/build_windows.ps1` | Compilación en Windows (MSVC 2022 + Ninja, `-Cuda -CudaArch N`) |
| `eval/` | Harness Python de la fase 0 (llama-server + vela3 ONNX) y set sintético `es_marketing_v1` |

## Compilar y probar (Windows)

```powershell
git submodule update --init --depth 1
```

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -Cuda -CudaArch 89
```

- La primera compilación tarda de 30 a 60 minutos y las incrementales, pocos minutos.
- La extensión queda en `build/release/extension/decide/decide.duckdb_extension`. Se carga con el DuckDB CLI **2.0.0** iniciado con `duckdb -unsigned`.

Pruebas, desde la raíz del repo y con `-unsigned`:
- `test/parity/prompt_parity.sql`: los 226 prompts deben salir idénticos.
- `test/decide_clause_demo.sql`: demo de `DECIDE`.
- `test/parity/e2e_0.8b.sql` + `compare_0.8b.sql`: paridad de probabilidades con el harness. Necesita `eval/results.duckdb`, que no está en git; se genera con `eval/run_eval.py`.
- `test/sql/*.test` (sqllogictest): todavía **no** se ejecutan, porque el build tiene `BUILD_UNITTESTS=FALSE`.

Los modelos se descargan solos a `~/.duckdb/decide/models`. También se puede usar `SET decide_model_dir` o `SELECT decide_download('onejev-4b')`.

## Trampas conocidas (no repetir)

- **Versión exacta.** El build usa `-DDUCKDB_EXPLICIT_VERSION=v2.0.0` y `-DDUCKDB_CAPABILITIES="httplib loadable_extensions"`. Con un `git describe` tipo `v2.0.0-0-g…` la extensión no casa con el CLI oficial y `INSTALL httpfs` apunta a un repositorio local.
- **DuckDB 2.0: toda función escalar que pueda lanzar errores necesita `SetFallible()`.** Sin eso, un error normal sale como `INTERNAL Error`.
- **DuckDB 2.0, otros cambios de API:**
  - Accesores de vectores de solo lectura por defecto: `FlatVector::GetDataMutable`, `ValidityMutable`, `ListVector::GetChildMutable`.
  - Las funciones dentro de un `FunctionSet` son inmutables: fija `SetVolatile`/`SetFallible` antes de `AddFunction`.
  - Los binds de tablas usan `vector<Identifier>`.
- **llama.cpp b12521:** `llama_batch_init`/`llama_decode` están obsoletas; se usa `llama_batch_ext_*` + `llama_process`. El GGUF de OneJev declara una capa MTP (`nextn_predict_layers=1`) que las versiones viejas, como llama‑cpp‑python 0.3.36, no cargan.
- **CUDA 13 no soporta Pascal** (GTX 10xx, Quadro P). Para esas GPU: CUDA 12.x y `-CudaArch 61`.
- **No cambies el orden del prompt.** Poner la pregunta antes del texto (para cachearla) bajó el 0.8B de 85.8 % a 45.1 %.
- **La gramática `DECIDE` se activa a partir de la consulta siguiente al `LOAD`.** En el CLI, `LOAD` y `DECIDE` no pueden ir en la misma cadena `-c`; usa `-cmd "LOAD …"`.
- La CPU de la máquina donde se desarrolló (i3 + Quadro P600) no es representativa. **Las velocidades se miden en el equipo de destino.**

## Siguiente paso en la laptop del usuario

Equipo: i9‑13900H, 32 GB, **RTX 2000 Ada Laptop de 8 GB** (`sm_89`, 35 W) y driver 596.71 con CUDA 13.2.

1. Instalar VS 2022 Build Tools (C++), CUDA Toolkit 13.x y DuckDB CLI 2.0.0.
2. Compilar con `-Cuda -CudaArch 89`. Esta compilación **nunca se ha probado**; si falla, corregir aquí: CMake de llama.cpp con `GGML_CUDA`, la exportación de los objetivos `ggml-cuda` en `CMakeLists.txt` y las arquitecturas.
3. Verificar que `SELECT decide_version()` lista `CUDA` en los backends.
4. Medir en GPU con el set de evaluación (`test/parity/e2e_0.8b.sql`, adaptado al 4B y al 9B) y anotar en `PLAN.md` los tokens/s de `decide_stats()`. Ajustar `decide_parallel` y `decide_batch_tokens`.
5. Pendientes de rendimiento en `PLAN.md`: prefijo compartido con `llama_memory_seq_cp` (verificar que funcione con la memoria recurrente de Qwen3.5) y tokenizar las partes fijas una sola vez.
