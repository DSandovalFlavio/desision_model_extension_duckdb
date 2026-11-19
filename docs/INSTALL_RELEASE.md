# decide_amox: instalación rápida (Windows)

1. **DuckDB:** instala el DuckDB CLI **2.0.0** desde https://github.com/duckdb/duckdb/releases/tag/v2.0.0. Otras versiones no cargan esta extensión.
2. **Solo en el zip `cuda12`:** copia `cudart64_12.dll`, `cublas64_12.dll` y `cublasLt64_12.dll` junto a `duckdb.exe`, o añade esta carpeta al `PATH`. Necesitas el driver de NVIDIA actualizado; no hace falta el CUDA Toolkit.
3. **Carga la extensión:**
   ```
   duckdb -unsigned
   LOAD 'C:/ruta/a/esta/carpeta/decide_amox.duckdb_extension';
   SELECT amox_version();
   ```
   En el zip `cuda12`, `amox_version()` debe listar `CUDA`.
4. **Clasifica:**
   ```
   SELECT amox_yes_no('Pierde 15 kilos en 7 días', '¿Este anuncio promete resultados milagrosos?');
   ```
   La primera consulta descarga el modelo `onejev-4b` (2.7 GB). Para usar modelos copiados a mano, sin internet, ve a https://github.com/DSandovalFlavio/desision_model_extension_duckdb/blob/HEAD/docs/OFFLINE.md

Documentación completa: https://github.com/DSandovalFlavio/desision_model_extension_duckdb
