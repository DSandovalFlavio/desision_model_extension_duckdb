# decide: modelos de decisión en DuckDB

Extensión de **DuckDB 2.0** para clasificar texto desde SQL con modelos de decisión tipo Jev, en local, usando la familia **OneJev1.6** (Apache‑2.0) sobre llama.cpp. Entra un texto, una pregunta y unas opciones; sale una probabilidad por opción en una sola pasada, sin generar texto.

| Modelo | Tamaño GGUF | Acierto fase 0 (es) | Uso |
|---|---|---|---|
| `onejev-0.8b` | 0.8 GB | 85.8 % | Rápido, volumen alto |
| **`onejev-4b`** (por defecto) | 2.7 GB | **96.0 %** | Equilibrio |
| `onejev-9b` | 5.6 GB | 100 % (95 anuncios) | Máxima calidad, cabe en 8 GB de VRAM |

Evaluación en [eval/RESULTS.md](eval/RESULTS.md) · plan en [PLAN.md](PLAN.md).

> **Estado: fase 2.**
> - Clasificación funcionando: `decision_choose`, `decision_yes_no` y la cláusula `DECIDE`, con lotes multisecuencia, deduplicación y caché LRU.
> - Los modelos se descargan solos la primera vez, verificados con SHA‑256.
>
> **Verificado:** Windows x64 con MSVC 2022, compilación **solo CPU**, cargada en el DuckDB **2.0.0 oficial**.
>
> **Aún sin verificar:**
> - La compilación con **CUDA**.
> - Linux y macOS (`make release`).
> - Las pruebas `test/sql/*.test` con el runner de DuckDB.
>
> Pendiente también: el resto de optimizaciones de [PLAN.md](PLAN.md).

## Inicio rápido (Windows)

**1. Requisitos (una sola vez)**
- [Git](https://git-scm.com/download/win).
- [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/) con la carga de trabajo **"Desarrollo para el escritorio con C++"**. Incluye MSVC, CMake y Ninja.
- Para usar la GPU NVIDIA: [CUDA Toolkit](https://developer.nvidia.com/cuda-downloads) 12.x o 13.x. Con GPUs Pascal (GTX 10xx, Quadro P) usa 12.x.
- [DuckDB CLI **2.0.0**](https://github.com/duckdb/duckdb/releases/tag/v2.0.0) (no la 1.x).

**2. Clonar con los submódulos** (DuckDB v2.0.0, extension-ci-tools y llama.cpp b12521, fijados):

```powershell
git clone https://github.com/<tu-usuario>/<tu-repo>.git decide
```

```powershell
cd decide
```

```powershell
git submodule update --init --depth 1
```

**3. Compilar.** Desde una terminal normal; el script carga solo el entorno de Visual Studio. La primera vez tarda entre 30 y 60 minutos, porque compila DuckDB y llama.cpp.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -Cuda -CudaArch 89
```

- Usa `-CudaArch 89` para RTX 40xx, `86` para RTX 30xx y `75` para RTX 20xx.
- Sin GPU NVIDIA, quita `-Cuda -CudaArch 89`.

**4. Usar**

```powershell
duckdb -unsigned
```

```sql
LOAD 'build/release/extension/decide/decide.duckdb_extension';
SELECT decide_version();          -- con CUDA debe listar "CUDA" en backends
SELECT decide_download();         -- baja onejev-4b (2.7 GB) a ~/.duckdb/decide/models
SELECT decision_yes_no('Pierde 15 kilos en 7 días sin dieta', '¿Este anuncio infringe las políticas de Google Ads?');
```

## Cláusula DECIDE

```sql
SELECT id, sentimiento, sentimiento_confidence, queja
FROM comentarios DECIDE (
    CHOOSE '¿Qué sentimiento expresa este comentario sobre la marca?' AMONG (
        positivo 'Opinión favorable: satisfacción, elogio o recomendación',
        negativo 'Queja, enojo, decepción o crítica (incluye sarcasmo)',
        neutro   'Pregunta, dato o comentario informativo',
        mixto    'Combina claramente aspectos positivos y negativos'
    ) AS sentimiento,
    ASK '¿El cliente se está quejando de algo?' AS queja
    GIVEN texto
);
```

- **Columnas que añade cada pregunta:**
  - `CHOOSE … AS x` añade `x` (la opción elegida), `x_confidence` y `x_probabilities`.
  - `ASK … AS y` añade `y` (BOOLEAN, p ≥ 0.5), `y_confidence` y `y_probabilities` con `yes`/`no`.
- **Qué lee el modelo (`GIVEN`):**
  - Con una columna, el modelo lee su texto tal cual.
  - Con varias, `GIVEN canal, texto` las convierte en un objeto JSON con sangría 2, el formato con el que se entrenó OneJev. Lo puedes ver con `decide_state(...)`.
- **Forma de función:** `decision_choose('pregunta' AMONG (a, b, c) GIVEN col1, col2)` y `decision_yes_no('pregunta' GIVEN col)`.
- **Mismo motor:** la cláusula se reescribe a `decision_choose` / `decision_yes_no`, así que usa los mismos lotes y la misma caché.
- **Activación:** la gramática se activa en las conexiones abiertas después del `LOAD`. En la conexión que ejecutó `LOAD`, se activa a partir de la consulta siguiente, así que `LOAD` y `DECIDE` no pueden ir en la misma cadena `-c`.

## Uso

```sql
-- iniciar DuckDB 2.0.0 con: duckdb -unsigned
LOAD 'build/release/extension/decide/decide.duckdb_extension';
SET decide_model_dir = 'C:/modelos';     -- busca <dir>/<archivo>.gguf o <dir>/<modelo>/<archivo>.gguf

-- elegir una opción: MAP nombre -> descripción (recomendado) o lista de nombres
SELECT texto,
       decision_choose(texto, '¿Qué sentimiento expresa este comentario sobre la marca?', MAP {
           'positivo': 'Opinión favorable: satisfacción, elogio o recomendación',
           'negativo': 'Queja, enojo, decepción o crítica (incluye sarcasmo)',
           'neutro':   'Pregunta, dato o comentario informativo',
           'mixto':    'Combina claramente aspectos positivos y negativos'}) AS s
FROM comentarios;
-- s.choice, s.confidence, s.probabilities['negativo']

-- sí/no: probabilidad de "sí"
SELECT decision_yes_no(anuncio, '¿Este anuncio infringe las políticas de Google Ads?') AS p_infringe FROM anuncios;

-- modelo por consulta o por sesión
SELECT decision_choose(texto, 'pregunta', ['a', 'b'], 'onejev-0.8b') FROM t;
SET decide_model = 'onejev-9b';          -- 'onejev-0.8b' | 'onejev-4b' (por defecto) | 'onejev-9b'

-- utilidades
SELECT * FROM decide_models();           -- modelos, descargados, cargados
SELECT * FROM decide_stats();            -- decisiones, aciertos de caché, tokens/s, modelo cargado
SELECT decide_prompt(texto, 'pregunta', ['a', 'b']);   -- el prompt exacto que ve el modelo
SELECT decide_tokenize('hola');
SELECT decide_clear_cache();
```

| Ajuste | Por defecto | Qué hace |
|---|---|---|
| `decide_model` | `onejev-4b` | Modelo de la sesión |
| `decide_model_dir` | `~/.duckdb/decide/models` | Carpeta donde se buscan los GGUF |
| `decide_model_path` | vacío | Ruta explícita del GGUF del modelo actual |
| `decide_parallel` | 16 | Textos evaluados a la vez por llamada a la GPU |
| `decide_batch_tokens` | 4096 | Tokens por llamada; también es el tamaño de la caché KV compartida |
| `decide_gpu_layers` | -1 | Capas en GPU (-1 = todas, 0 = solo CPU) |
| `decide_kv_cache` | `f16` | `q8_0` usa menos VRAM (útil con el 9B y mucho paralelismo) |
| `decide_threads` | 0 | Hilos de CPU para llama.cpp (0 = todos) |
| `decide_max_tokens` | 1024 | Límite por prompt; los textos más largos se truncan |
| `decide_cache_size` | 100000 | Respuestas guardadas en la caché LRU (0 = desactivada) |
| `decide_auto_download` | `true` | Descarga los modelos que falten desde Hugging Face |

**Consejo de rendimiento:** la pregunta y las descripciones de las opciones se procesan en cada fila y suelen pesar más que el texto (en los anuncios son unos 259 tokens frente a 25 del texto). Descripciones breves aceleran mucho la clasificación.

## Modelos: descarga automática

- **Cuándo se descarga:** la primera consulta que usa un modelo lo descarga de Hugging Face a `decide_model_dir`, en `<dir>/<modelo>/<archivo>.gguf`. Por defecto la carpeta es `~/.duckdb/decide/models`.
- **Cómo se descarga:** a un archivo temporal, calculando el SHA‑256 mientras llega. Solo se renombra al nombre final si el hash coincide con el que lleva fijado la extensión. El progreso aparece en la consola.
- **Para no esperar en la primera consulta**, descárgalo antes:
  ```sql
  SELECT decide_download('onejev-4b');  -- o sin argumento: el modelo de la sesión
  ```
- **Archivos que ya tengas:** se usan si están en esa carpeta (`<dir>/<archivo>` o `<dir>/<modelo>/<archivo>`).
  - La primera vez se comprueba su hash y se deja un sello `.verified` junto al archivo, con tamaño y fecha. Las siguientes cargas no vuelven a calcular el hash.
  - Si el archivo está incompleto o es distinto, el error lo dice y no se usa.
- **Requisito:** necesita la extensión `httpfs`, que DuckDB instala y carga sola.
- **Sin conexión:** `SET decide_auto_download = false` y apunta `decide_model_dir` (o `decide_model_path`) a tus archivos.

| Modelo | Tamaño de descarga |
|---|---|
| `onejev-0.8b` | 0.8 GB |
| `onejev-4b` | 2.7 GB |
| `onejev-9b` | 5.6 GB |

## Detalles de compilación

- **Script:** `scripts/build_windows.ps1`. Busca Visual Studio 2022 con `vswhere`, carga `vcvars64` y compila DuckDB v2.0.0 junto con la extensión usando Ninja.
  - Opciones: `-Cuda`, `-CudaArch <sm>`, `-Portable` (sin optimizar para el CPU de la máquina que compila, para repartir el binario a otros equipos), `-Config debug|release` y `-Jobs N`.
- **Salidas:**
  - `build/release/extension/decide/decide.duckdb_extension`: la extensión cargable. Necesita DuckDB **2.0.0** exacto, iniciado con `-unsigned`.
  - `build/release/duckdb.exe`: un DuckDB 2.0.0 compilado aquí. La extensión **no** va incluida y se carga con `LOAD` igual que en el oficial.
- **Versión:** el build usa la versión exacta `v2.0.0` y descarga extensiones (por ejemplo, `httpfs`) del repositorio oficial. Así la extensión carga en el CLI oficial.
- **Linux/macOS:** `make release` (o `GEN=ninja make release`). Aún sin probar.

## Reproducir la evaluación (fase 0)

```powershell
python -m venv .venv
```

```powershell
.venv\Scripts\pip install llama-cpp-python==0.3.36 jinja2 huggingface_hub onnxruntime tokenizers duckdb numpy
```

```powershell
.venv\Scripts\python eval\download_models.py
```

- Descarga `llama-server` b12521 de [las releases de llama.cpp](https://github.com/ggml-org/llama.cpp/releases/tag/b12521) en `tools/llama-b12521-cuda12/`, o pasa `--server`.
- Después ejecuta `bash eval/run_all.sh` y `python eval/report.py`.
- El set `eval/data/es_marketing_v1.jsonl` es **sintético** (226 ejemplos de marketing en español). Sus cifras orientan la elección del modelo, pero no sustituyen una evaluación con tus datos.

## Licencias

- Extensión: MIT. La gramática `DECIDE` es un port de `altertable-ai/duckdb-decide_onnx` (MIT); detalles en [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- llama.cpp: MIT.
- DuckDB: MIT.
- Pesos de OneJev1.6: Apache‑2.0 (OmniJev). Los GGUF son conversiones de la comunidad (mradermacher).
