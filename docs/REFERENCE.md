# Referencia SQL de decide_amox

Todas las funciones y ajustes llevan el prefijo `amox_`, para no chocar con otras extensiones de decisión (`decide`, `decide_onnx`).

## Decisiones

### `amox_choose(texto, pregunta, opciones [, modelo])`

Elige una opción para cada fila.

| Argumento | Tipo | Descripción |
|---|---|---|
| `texto` | `VARCHAR` | Lo que el modelo lee (el "estado") |
| `pregunta` | `VARCHAR` | La instrucción, en español o en inglés |
| `opciones` | `MAP(VARCHAR, VARCHAR)` o `VARCHAR[]` | `MAP {'nombre': 'descripción', ...}` (recomendado) o `['a', 'b']` solo con nombres. De 2 a 26 opciones, sin repetir |
| `modelo` | `VARCHAR`, opcional | `'onejev-0.8b'`, `'onejev-4b'` o `'onejev-9b'`; si se omite, se usa `amox_model` |

Devuelve `STRUCT(choice VARCHAR, confidence DOUBLE, probabilities MAP(VARCHAR, DOUBLE))`. Si `texto`, `pregunta` u `opciones` es NULL, el resultado es NULL.

```sql
SELECT (amox_choose(texto, '¿Intención de búsqueda?', MAP {
          'informacional': 'Quiere aprender o resolver una duda',
          'transaccional': 'Quiere comprar o contratar ahora'})).choice
FROM keywords;
```

### `amox_yes_no(texto, pregunta [, modelo])`

Probabilidad de que la respuesta sea "sí", como `DOUBLE` entre 0 y 1.

```sql
SELECT anuncio FROM anuncios
WHERE amox_yes_no(anuncio, '¿Promete resultados de salud milagrosos?') >= 0.7;
```

### Cláusula `DECIDE`

```sql
FROM <tabla | subconsulta | función> DECIDE (
    CHOOSE <pregunta> AMONG (<opción> ['descripción'], ...) AS <nombre>,
    ASK    <pregunta> AS <nombre>,
    ...
    GIVEN <expresión> [AS <alias>], ...
) [alias]
```

- **Columnas que añade:**
  - Cada `CHOOSE … AS x` añade `x`, `x_confidence` y `x_probabilities`.
  - Cada `ASK … AS y` añade `y` (`BOOLEAN`, p ≥ 0.5), `y_confidence` y `y_probabilities` (`MAP` con `yes`/`no`).
- **`GIVEN`:**
  - Con una sola expresión, el modelo lee su texto tal cual.
  - Con varias, el modelo lee un objeto JSON con sangría 2 con esos nombres; es el formato con el que se entrenó OneJev. Puedes verlo con `amox_state(struct_pack(...))`.
- **Formas de función de la misma sintaxis:**
  ```sql
  SELECT amox_choose('¿Equipo?' AMONG (facturacion, soporte) GIVEN asunto, mensaje) FROM tickets;
  SELECT amox_yes_no('¿Pide reembolso?' GIVEN mensaje) FROM tickets;
  ```
- **Mismo motor:** la cláusula se reescribe a `amox_choose` y `amox_yes_no`, así que comparte lotes y caché con ellas.
- **Activación:** la gramática se activa en la consulta **siguiente** al `LOAD`. En el CLI no pongas `LOAD` y `DECIDE` en la misma cadena `-c`; usa `-cmd "LOAD …"` o `.duckdbrc`.

## Modelos y descarga

| Función | Devuelve | Descripción |
|---|---|---|
| `amox_models()` | tabla | `model`, `is_default`, `is_current`, `family`, `size`, `quant`, `file_mb`, `license`, `notes`, `url`, `sha256`, `local_path`, `downloaded`, `loaded` |
| `amox_download([modelo])` | `VARCHAR` | Descarga (o verifica) el modelo y devuelve su ruta local. Sin argumento, usa el modelo de la sesión |

## Diagnóstico

| Función | Devuelve | Descripción |
|---|---|---|
| `amox_stats()` | tabla | `decisions`, `cache_hits`, `deduplicated`, `evaluated`, `prompt_tokens`, `truncated`, `process_calls`, `seconds`, `tokens_per_s`, `decisions_per_s`, `loaded_model` |
| `amox_clear_cache()` | `BOOLEAN` | Vacía la caché de respuestas y reinicia las estadísticas |
| `amox_version()` | `VARCHAR` | Versión, build de llama.cpp y backends disponibles (`CUDA`, `Metal`, `CPU`…) |
| `amox_prompt(texto, pregunta, opciones)` / `amox_prompt(texto, pregunta)` | `VARCHAR` | El prompt exacto que ve el modelo |
| `amox_state(valor)` | `VARCHAR` | Cómo se presenta un valor al modelo: texto tal cual, el resto como JSON con sangría 2 |
| `amox_tokenize(texto [, modelo])` | `INTEGER[]` | Tokens del texto con el vocabulario del modelo |

## Ajustes (`SET …`)

| Ajuste | Por defecto | Descripción |
|---|---|---|
| `amox_model` | `onejev-4b` | Modelo de la sesión |
| `amox_model_dir` | `~/.duckdb/decide_amox/models` | Carpeta de los GGUF: `<dir>/<archivo>` o `<dir>/<modelo>/<archivo>` |
| `amox_model_path` | vacío | Ruta explícita al GGUF del modelo de la sesión (sin verificación de hash) |
| `amox_auto_download` | `true` | Descargar de Hugging Face los modelos que falten |
| `amox_parallel` | `16` | Textos evaluados a la vez por llamada al modelo |
| `amox_batch_tokens` | `4096` | Tokens por llamada; también es el tamaño de la caché KV compartida |
| `amox_gpu_layers` | `-1` | Capas en GPU (`-1` = todas, `0` = solo CPU) |
| `amox_kv_cache` | `f16` | `q8_0` reduce la VRAM |
| `amox_threads` | `0` | Hilos de CPU (`0` = todos) |
| `amox_max_tokens` | `1024` | Máximo de tokens por prompt; los textos más largos se truncan |
| `amox_cache_size` | `100000` | Respuestas en la caché LRU (`0` = sin caché) |

Cambiar `amox_model`, `amox_parallel`, `amox_batch_tokens`, `amox_gpu_layers`, `amox_threads` o `amox_kv_cache` recarga el modelo en la siguiente consulta. Solo hay un modelo cargado a la vez por proceso.

Variable de entorno `DECIDE_AMOX_VERBOSE=1`: muestra los registros de llama.cpp, útil para diagnosticar la GPU.

## Cómo decide el modelo

- **Prompt:** el formato es `qev-labels-v2` de OneJev. Las opciones se etiquetan A, B, C…, y la probabilidad de cada una es el softmax de los logits de su letra en una sola pasada. Para ver el prompt exacto, usa `amox_prompt(...)`.
- **Calibración:** las probabilidades no están calibradas para tus datos. Antes de fijar umbrales, como "revisar a mano si `confidence < 0.65`", compruébalos con un conjunto etiquetado.
- **Idioma:** la pregunta y las descripciones pueden estar en español o en inglés. En la evaluación, preguntas en español funcionaron bien con textos en español.
