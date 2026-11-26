# Compilar, CI y releases

Para **usar** la extensión no hace falta nada de esto: los binarios salen de GitHub Actions (ver el [README](../README.md)). Este documento es para quien modifica el código o publica versiones.

## Cómo se construye en GitHub

| Workflow | Cuándo corre | Qué hace |
|---|---|---|
| [`MainDistributionPipeline.yml`](../.github/workflows/MainDistributionPipeline.yml) | Cada push y PR | Compila con el pipeline oficial de DuckDB (`extension-ci-tools@v2.0-cyanoptera`) para Linux (amd64 y arm64), macOS (arm64) y Windows (amd64), solo CPU (Metal en macOS), y ejecuta `test/sql/*.test`. Es el mismo proceso que usa *Community Extensions* para firmar |
| [`Release.yml`](../.github/workflows/Release.yml) | Al **publicar un Release**, o a mano desde *Actions* | Compila para Windows las variantes `cpu` y `cuda12` (CUDA 12.4, arquitecturas `61;75;86;89`) y sube los zips y sus `.sha256` al release. Si se lanza a mano, los zips quedan como *artifacts* del workflow |

### Publicar una versión

1. Sube `EXTENSION_VERSION` en [`extension_config.cmake`](../extension_config.cmake) y añade la entrada a [`CHANGELOG.md`](../CHANGELOG.md).
2. Comprueba que el CI de la rama esté en verde.
3. En GitHub: **Releases → Draft a new release**, crea la etiqueta `vX.Y.Z` y pulsa **Publish**.
4. El workflow *Release* adjunta los zips al terminar. La variante CUDA tarda 1–2 horas; es gratis en repositorios públicos.
5. Si la extensión ya está en *Community Extensions*, actualiza `repo.ref` en su `description.yml` (ver [PUBLISHING.md](PUBLISHING.md)).

## Compilar en local (Windows)

**Requisitos:**
- Git.
- Visual Studio 2022 Build Tools con "Desarrollo para el escritorio con C++". Incluye MSVC, CMake y Ninja.
- Para GPU: CUDA Toolkit 12.x o 13.x. **CUDA 13 no soporta Pascal (GTX 10xx)**; para esas GPU usa 12.x.

```powershell
git clone https://github.com/DSandovalFlavio/desision_model_extension_duckdb.git decide_amox
```

```powershell
cd decide_amox
```

```powershell
git submodule update --init --depth 1
```

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -Cuda -CudaArch 89
```

| Opción | Efecto |
|---|---|
| `-Cuda` | Compila el backend CUDA de llama.cpp |
| `-CudaArch "61;75;86;89"` | Arquitecturas de GPU: `61` Pascal, `75` Turing, `86` Ampere, `89` Ada |
| `-Portable` | Sin optimizaciones específicas del CPU que compila; obligatorio para repartir el binario |
| `-Config debug` | Build de depuración |
| `-Jobs N` | Compilaciones en paralelo |

- **Tiempo:** la primera compilación tarda de 30 a 60 minutos (DuckDB + llama.cpp); las incrementales, pocos minutos.
- **Salida:** `build/release/extension/decide_amox/decide_amox.duckdb_extension`.
- **Versión:** el script fija la versión exacta `v2.0.1` (`DUCKDB_EXPLICIT_VERSION`) y quita el repositorio local de extensiones (`DUCKDB_CAPABILITIES`). Así el binario carga en el DuckDB 2.0.1 oficial e instala `httpfs` del repositorio oficial.

## Compilar en local (Linux / macOS)

```bash
git submodule update --init --depth 1
```

```bash
GEN=ninja make release
```

Para CUDA en Linux: `EXT_FLAGS="-DDECIDE_WITH_CUDA=ON" GEN=ninja make release`. Sin probar fuera del CI.

## Pruebas

| Prueba | Cómo se ejecuta | Necesita modelo |
|---|---|---|
| `test/sql/*.test` (sqllogictest) | El CI con `make test` | No |
| `test/parity/prompt_parity.sql` | `duckdb -unsigned -c ".read test/parity/prompt_parity.sql"`: los 226 prompts deben ser idénticos a los de `llama-server` | No |
| `test/parity/e2e_0.8b.sql` + `compare_0.8b.sql` | Paridad de probabilidades frente al harness de Python; necesita `eval/results.duckdb` | Sí (0.8B) |
| `test/parity/cache_dedup.sql` | Deduplicación y caché | Sí (0.8B) |
| `test/manual/*.sql` | Demo de `DECIDE` y prueba de humo | Sí |

## Estructura del código

| Archivo | Qué hace |
|---|---|
| `src/decide_amox_extension.cpp` | Registro de funciones SQL y ajustes; `amox_state` (JSON al estilo de Python) |
| `src/decide_runtime.cpp` | Motor llama.cpp: carga, lotes multisecuencia, logits del último token, deduplicación, caché LRU, estadísticas |
| `src/decide_prompt.cpp` | Prompt `qev-labels-v2` con plantilla de chat Qwen3.5 (idéntico byte a byte al de referencia) |
| `src/decide_grammar.cpp` | Cláusula `DECIDE` (gramática PEG) |
| `src/decide_store.cpp` | Descarga con `httpfs`, SHA‑256, archivo temporal + rename y sello `.verified` |
| `src/decide_models.cpp` | Los 3 modelos con URL, tamaño y SHA‑256 fijados |
| `third_party/llama.cpp` | Copia **incluida** (no es un submódulo) de llama.cpp b12521: solo CMake, `include/`, `src/`, `vendor/` y los backends CPU, BLAS, Metal y CUDA. Ver `VENDORED.md` y la sección "Actualizar llama.cpp" |
| `duckdb`, `extension-ci-tools` | Submódulos fijados: v2.0.1 y v2.0-cyanoptera |

## Actualizar llama.cpp

`third_party/llama.cpp` es una copia parcial, no un submódulo: el árbol completo de llama.cpp tiene rutas más largas que el límite de Windows (`ui/…`), y eso rompe el checkout con submódulos en los runners de Windows del CI de DuckDB. Para cambiar de versión:

```bash
git clone --depth 1 --branch <tag> https://github.com/ggml-org/llama.cpp /tmp/llama.cpp
```

```bash
python scripts/vendor_llama_cpp.py /tmp/llama.cpp <tag>
```

Después, actualiza `DECIDE_LLAMA_BUILD` en `CMakeLists.txt`, recompila y repite las pruebas de paridad (`test/parity/`).


## Pasar a una nueva versión de DuckDB

Las extensiones C++ solo cargan en la versión **exacta** de DuckDB para la que se compilaron. Con cada versión nueva, incluidas las de correcciones como 2.0.0 → 2.0.1, hay que publicar una versión nueva de decide_amox:

1. **Submódulos:**
   - `git -C duckdb fetch --depth 1 origin refs/tags/vX.Y.Z` y después `git -C duckdb checkout FETCH_HEAD`.
   - `extension-ci-tools`: la rama de esa versión, por ejemplo `v2.0-cyanoptera`.
2. **`vX.Y.Z` en:**
   - `scripts/build_windows.ps1` (`DUCKDB_EXPLICIT_VERSION`);
   - `.github/workflows/MainDistributionPipeline.yml` (`duckdb_version`);
   - los enlaces de descarga del CLI en `README.md`, `docs/OFFLINE.md` y `docs/INSTALL_RELEASE.md`.
3. **Versión de la extensión:** sube `EXTENSION_VERSION` en `extension_config.cmake` y añade la entrada en `CHANGELOG.md`.
4. **Compilación y pruebas:** compila en local, ejecuta `test/parity/prompt_parity.sql` con el CLI nuevo y espera el CI en verde.
5. **Publicación:** publica el Release (ver arriba).

Si la extensión está en *Community Extensions*, DuckDB la recompila solo en cada versión nueva y estos pasos solo hacen falta para los zips de GitHub.
