# Publicar en DuckDB Community Extensions

Con la extensión publicada ahí, cualquiera la instala **firmada por DuckDB**, sin `-unsigned`:

```sql
INSTALL decide_amox FROM community;
LOAD decide_amox;
```

**Limitación:** el CI de DuckDB no tiene CUDA, así que la versión de *community* usa **CPU** en Windows y Linux y **Metal** en macOS. Para GPU NVIDIA sigue haciendo falta el zip `cuda12` de los releases de este repo.

## Pasos

1. **CI en verde:** el workflow *Main Extension Distribution Pipeline* debe pasar en el commit que se va a publicar. Usa el mismo pipeline que el CI de DuckDB.
2. **Fork** de [duckdb/community-extensions](https://github.com/duckdb/community-extensions) y un archivo `extensions/decide_amox/description.yml`. Borrador:

```yaml
extension:
  name: decide_amox
  description: Classify text in SQL with local OneJev1.6 decision models (0.8B/4B/9B) through llama.cpp - choices, yes/no probabilities and a DECIDE clause, no external API
  version: '0.1.1'
  language: C++
  build: cmake
  license: MIT
  excluded_platforms: "wasm_mvp;wasm_eh;wasm_threads;windows_amd64_mingw;linux_amd64_musl;osx_amd64"
  maintainers:
    - DSandovalFlavio

repo:
  github: DSandovalFlavio/desision_model_extension_duckdb
  ref: <commit con el CI en verde>

docs:
  hello_world: |
    INSTALL decide_amox FROM community;
    LOAD decide_amox;
    -- The default model (OneJev1.6-4B, 2.7 GB, Apache-2.0) is downloaded from Hugging Face on first use.
    SELECT amox_yes_no('Lose 15 kg in 7 days with this miracle pill',
                       'Does this ad make misleading health claims?') AS p_yes;
    SELECT * FROM (VALUES ('I love it'), ('Broken on arrival')) t(review) DECIDE (
        CHOOSE 'What is the sentiment?' AMONG (positive, negative, neutral) AS sentiment
        GIVEN review);
  extended_description: |
    decide_amox runs Jev-style "System One" decision models locally: one forward pass per decision returns a
    probability for every option, without generating text. It ships the OneJev1.6 family (Apache-2.0) in three
    sizes, downloaded from Hugging Face on first use and verified by SHA-256. Rows are batched across sequences,
    deduplicated and cached. Builds from community extensions run on CPU (Metal on macOS); a CUDA build for
    Windows is published in the GitHub releases.
```

3. **PR** a `duckdb/community-extensions`. Su CI compila y prueba en todas las plataformas no excluidas. Cuando lo aprueban, la extensión queda publicada y firmada.
4. **Actualizaciones:** cambia `repo.ref` (y `version`) en un PR nuevo. Con cada versión nueva de DuckDB, ellos recompilan la extensión automáticamente si sigue siendo compatible. Para preparar una versión futura de DuckDB, ver su [UPDATING.md](https://github.com/duckdb/community-extensions/blob/main/UPDATING.md).

## Antes de enviar el PR

- [ ] CI en verde en el `ref` elegido.
- [ ] `test/sql/*.test` pasan en el CI y no descargan modelos.
- [ ] Ningún nombre de función choca con extensiones existentes; el prefijo `amox_` lo evita.
- [ ] README y `docs/` actualizados con la versión.
