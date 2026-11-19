# decide_amox: guía para agentes

Extensión de **DuckDB 2.0.0** (C++17) que clasifica texto desde SQL con modelos de decisión **OneJev1.6** (0.8B / **4B por defecto** / 9B, GGUF, Apache‑2.0) sobre **llama.cpp b12521**, enlazado estáticamente. Una pasada por decisión: la probabilidad es el softmax de los logits de las letras A, B, C… del prompt `qev-labels-v2`.

El usuario escribe en español; responde en español. El uso es **comercial**: solo modelos con licencia permisiva (StartLux y openjev son CC BY‑NC y están descartados).

## Dónde está cada cosa

| Documento | Para quién |
|---|---|
| [README.md](README.md) | Usuarios: instalar desde los Releases o Community, primeros pasos |
| [docs/REFERENCE.md](docs/REFERENCE.md) | Referencia SQL completa (funciones, `DECIDE`, ajustes) |
| [docs/OFFLINE.md](docs/OFFLINE.md) | Repartir extensión y modelos sin internet (Drive/USB) |
| [docs/BUILDING.md](docs/BUILDING.md) | Compilar, workflows de CI/release, publicar versiones, pruebas, mapa del código |
| [docs/PUBLISHING.md](docs/PUBLISHING.md) | DuckDB Community Extensions + borrador de `description.yml` |
| [docs/EVALUATION.md](docs/EVALUATION.md), [eval/RESULTS.md](eval/RESULTS.md) | Por qué OneJev 4B y cómo reproducir la evaluación |
| [docs/DESIGN.md](docs/DESIGN.md) | Historial de investigación y decisiones; usa los nombres antiguos (`decision_*`, `decide_*`) |
| [CHANGELOG.md](CHANGELOG.md), [SECURITY.md](SECURITY.md) | Versiones y modelo de seguridad |

**Para usarla no se compila:** los binarios salen de GitHub Actions. [`MainDistributionPipeline.yml`](.github/workflows/MainDistributionPipeline.yml) hace el CI CPU multiplataforma con el pipeline oficial de DuckDB, y [`Release.yml`](.github/workflows/Release.yml) genera los zips `cpu` y `cuda12` de Windows en cada Release. Solo se compila en local para cambiar el código.

## Nombres

- La extensión se llama `decide_amox`, porque `decide` ya existe en *community*.
- Todas las funciones y ajustes SQL usan el prefijo `amox_`: `amox_choose`, `amox_yes_no`, `amox_models`, `amox_download`, `amox_stats`, `amox_prompt`, `amox_state`, `amox_tokenize`, `amox_version`, `amox_clear_cache`, `SET amox_*`.
- La cláusula `DECIDE` se mantiene, con reglas PEG `AmoxDecide*` y las formas de función `amox_choose(q AMONG … GIVEN …)` y `amox_yes_no(q GIVEN …)`.
- El código C++ conserva los nombres de archivo `decide_*.cpp`, el namespace `decide::` y las opciones CMake `DECIDE_*`.

## Trampas conocidas (no repetir)

- **Versión exacta.** El build usa `-DDUCKDB_EXPLICIT_VERSION=v2.0.0` y `-DDUCKDB_CAPABILITIES="httplib loadable_extensions"`. Con un `git describe` tipo `v2.0.0-0-g…` la extensión no casa con el CLI oficial e `INSTALL httpfs` apunta a un repositorio local.
- **DuckDB 2.0: toda función escalar que pueda lanzar errores necesita `SetFallible()`.** Sin eso, un error normal sale como `INTERNAL Error`.
- **Otros cambios de API en DuckDB 2.0:**
  - Accesores de vectores de solo lectura por defecto: `FlatVector::GetDataMutable`, `ValidityMutable`, `ListVector::GetChildMutable`.
  - Las funciones dentro de un `FunctionSet` son inmutables: fija `SetVolatile`/`SetFallible` antes de `AddFunction`.
  - Los binds de tablas usan `vector<Identifier>`.
- **llama.cpp b12521 está incluido como copia parcial**, no como submódulo, por las rutas largas de su `ui/` en Windows. Para actualizarlo usa `scripts/vendor_llama_cpp.py` y no edites `third_party/llama.cpp` a mano.
- **API de llama.cpp b12521:**
  - Usa `llama_batch_ext_*` + `llama_process`; `llama_batch_init`/`llama_decode` están obsoletas.
  - El GGUF de OneJev declara una capa MTP que llama.cpp antiguo no carga.
  - Los objetivos estáticos (`llama ggml ggml-base ggml-cpu` y, con CUDA, `ggml-cuda`) deben estar en `install(EXPORT DuckDBExports)`.
- **`DECIDE_NATIVE` es OFF por defecto** para que los binarios del CI sean portables. El script local lo activa salvo con `-Portable`.
- **CUDA 13 no soporta Pascal.** El release usa CUDA 12.4 con `61;75;86;89`.
- **No cambies el orden del prompt.** Poner la pregunta antes del texto bajó el 0.8B de 85.8 % a 45.1 %.
- En `src/decide_grammar.cpp`, el literal `"decide"` de `TransformDecideTableRef` es la palabra clave leída como alias, no el nombre de la extensión.
- **`DECIDE` se activa en la consulta siguiente al `LOAD`.** En el CLI usa `-cmd "LOAD …"`, no la misma cadena `-c`.
- **Las pruebas `test/sql/*.test` nunca deben descargar modelos:** usan `SET amox_auto_download = false`.
- **Velocidad:** la máquina donde se desarrolló (i3 + Quadro P600) no es representativa. Las velocidades se miden en el equipo de destino.

## Estado y siguientes pasos

- **Verificado en local:** Windows x64 con el DuckDB 2.0.0 oficial (CPU), paridad de prompts 226/226, paridad de predicciones, `DECIDE`, descarga y caché.
- **Verificado en el CI (19‑nov‑2026):**
  - El *Main Extension Distribution Pipeline* compila y pasa los 3 `test/sql/*.test` en `linux_amd64`, `linux_arm64`, `osx_arm64` y `windows_amd64`.
  - El zip `cpu` de *Release* funciona con el DuckDB 2.0.0 oficial.
  - Revisa siempre el run más reciente de Actions antes de dar algo por hecho.
- **Problemas del CI ya resueltos (no repetir):**
  - Rutas largas de llama.cpp en Windows: por eso llama.cpp está incluido como copia parcial.
  - Falta de `vcpkg.json`.
  - Exportación de `ggml-metal`/`ggml-blas` en macOS.
  - Paso de parámetros por arreglo en PowerShell: usar splatting con tabla hash.
- **Equipos del usuario:**
  - Laptop: i9‑13900H, 32 GB, RTX 2000 Ada 8 GB (`sm_89`), driver con CUDA 13.2. Usa el zip `cuda12` del release.
  - PC del hermano: GTX 1050 Ti 4 GB (`sm_61`). Mismo zip, con el 0.8B o el 4B y `amox_parallel` bajo.
- **Pendientes:**
  - Medir tokens/s en GPU real y ajustar `amox_parallel` y `amox_batch_tokens`.
  - Enviar el PR a Community Extensions ([docs/PUBLISHING.md](docs/PUBLISHING.md)).
  - Optimizaciones de [docs/DESIGN.md](docs/DESIGN.md): prefijo compartido con `llama_memory_seq_cp` y tokenizar una vez las partes fijas.
