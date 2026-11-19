# Uso sin internet (equipos sin acceso a Hugging Face o a GitHub)

Nada de esto requiere compilar: los modelos `.gguf` son archivos listos para usar y la extensión viene ya compilada en los [Releases](https://github.com/DSandovalFlavio/desision_model_extension_duckdb/releases).

## 1. Prepara una carpeta para compartir (Drive, USB, red)

```
decide_amox/
  decide_amox.duckdb_extension          <- del zip del release (cuda12 o cpu)
  cudart64_12.dll  cublas64_12.dll  cublasLt64_12.dll   <- solo en la variante cuda12
  modelos/
    LEEME.txt                           <- "OneJev1.6 (OmniJev), Apache-2.0" + enlace a la licencia
    onejev-0.8b/OneJev1.6-0.8B.Q8_0.gguf
    onejev-4b/OneJev1.6-4B.Q4_K_M.gguf
    onejev-9b/OneJev1.6-9B.Q4_K_M.gguf
```

- **De dónde salen los modelos:** de cualquier equipo que ya los haya descargado (`~/.duckdb/decide_amox/models/`). No copies los archivos `.verified`.
- **Licencia:** los pesos son Apache‑2.0, que permite redistribuirlos si se incluye el aviso de licencia. Revisa también que la política de tu empresa permita compartir archivos por ese medio.

## 2. En cada equipo

1. Instala el [DuckDB CLI 2.0.0](https://github.com/duckdb/duckdb/releases/tag/v2.0.0).
2. Copia la carpeta a disco local, por ejemplo `C:\decide_amox`.
3. Con la variante `cuda12`, copia las tres DLL junto a `duckdb.exe`, o añade `C:\decide_amox` al `PATH`.
4. Crea `C:\Users\<usuario>\.duckdbrc`:
   ```sql
   LOAD 'C:/decide_amox/decide_amox.duckdb_extension';
   SET amox_auto_download = false;            -- nunca intentar Hugging Face
   SET amox_model_dir = 'C:/decide_amox/modelos';
   ```
5. Abre DuckDB con `duckdb -unsigned`. La extensión de los releases no está firmada por DuckDB; ver el README.

La primera vez que se usa cada modelo, la extensión **verifica su SHA‑256** (unos segundos) y deja un sello `.verified`. Si una copia llegó corrupta o incompleta, verás un error claro en lugar de resultados erróneos.

## Google Drive para escritorio

Se puede apuntar `amox_model_dir` directamente a la carpeta sincronizada. Márcala como **"Disponible sin conexión"**: si el archivo no está descargado del todo, el modelo no carga.
