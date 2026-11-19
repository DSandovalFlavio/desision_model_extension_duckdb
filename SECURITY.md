# Seguridad

## Qué hace la extensión en tu equipo

- **Red:** solo descarga los archivos de modelo de Hugging Face (`huggingface.co/mradermacher/OneJev1.6-*-GGUF`), con la extensión oficial `httpfs` de DuckDB, y solo cuando un modelo no está en disco. Con `SET amox_auto_download = false` no usa la red. **Nunca envía tus datos.**
- **Integridad de los modelos:**
  - La URL, el tamaño y el **SHA‑256** de cada modelo están fijados en el código ([`src/decide_models.cpp`](src/decide_models.cpp)).
  - Las descargas van a un archivo temporal y solo se renombran si el hash coincide.
  - Los archivos que ya estaban en disco se verifican antes de usarse por primera vez.
  - `amox_model_path` es la excepción: apunta a un archivo explícito y **no** se verifica.
- **Disco:** escribe solo en `amox_model_dir` (por defecto `~/.duckdb/decide_amox/models`).

## Binarios

- **Releases de GitHub:** los zips los compila el workflow [`Release.yml`](.github/workflows/Release.yml) a partir del código etiquetado, y cada uno va acompañado de su `.sha256`. No están firmados por DuckDB, por eso se cargan con `-unsigned`. Cárgalos solo si vienen de este repositorio.
- **Community Extensions:** la versión de *community*, cuando esté publicada, la compila y firma el CI de DuckDB.

## Reportar un problema

Abre un *issue* en GitHub o, si es sensible, usa **Security → Report a vulnerability** del repositorio.
