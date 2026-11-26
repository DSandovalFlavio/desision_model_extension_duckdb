# Changelog

## 0.1.1 (2026-11-25)

- **DuckDB 2.0.1:** la extensión se compila para DuckDB v2.0.1, la versión de correcciones publicada el 20‑nov‑2026. Igual que todas las extensiones C++, solo carga en esa versión exacta; para DuckDB 2.0.0 usa decide_amox 0.1.0.
- **Windows con CUDA, las DLL junto a la extensión:**
  - Basta con dejar `cudart64_12.dll`, `cublas64_12.dll` y `cublasLt64_12.dll` en la misma carpeta que `decide_amox.duckdb_extension`. Ya no hace falta ponerlas junto a `duckdb.exe` ni en el `PATH`.
  - Antes, al hacer `INSTALL` o al cargarla desde otra aplicación, Windows fallaba con "No se puede encontrar el módulo especificado".
  - La extensión carga ahora esas DLL en diferido y las busca primero en su propia carpeta.
  - Si falta alguna, `LOAD` dice cuál y qué hacer.

## 0.1.0 (2026-11-19)

Primera versión.

- **Funciones:** `amox_choose` y `amox_yes_no`, la cláusula `FROM … DECIDE (CHOOSE … / ASK … GIVEN …)` y sus formas de función `amox_choose(q AMONG … GIVEN …)` y `amox_yes_no(q GIVEN …)`.
- **Modelos:** OneJev1.6 0.8B, 4B (por defecto) y 9B en GGUF, con descarga automática desde Hugging Face y verificación SHA‑256. Funcionan sin internet con `amox_model_dir`.
- **Motor llama.cpp b12521:**
  - lotes de varias secuencias;
  - logits solo del último token;
  - un único contexto por proceso;
  - deduplicación y caché LRU;
  - caché KV `q8_0` opcional;
  - truncado de textos largos.
- **Utilidades:** `amox_models`, `amox_download`, `amox_stats`, `amox_clear_cache`, `amox_version`, `amox_prompt`, `amox_state` y `amox_tokenize`, más 11 ajustes `amox_*`.
- **Builds:** CI multiplataforma (CPU) con el pipeline oficial de DuckDB 2.0, y releases para Windows `cpu` y `cuda12`.
- **Paridad verificada:** los 226 prompts son idénticos a los de la referencia y las 226 predicciones coinciden con el harness de Python.
