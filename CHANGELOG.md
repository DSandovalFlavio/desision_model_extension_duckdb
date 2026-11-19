# Changelog

## 0.1.0 (sin publicar)

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
