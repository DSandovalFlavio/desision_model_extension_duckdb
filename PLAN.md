# Extensión DuckDB de modelos de decisión (español, multi‑modelo, local)

**Objetivo:** clasificar ~10k textos en español desde SQL, con modelos de decisión tipo Jev ("System One": entra un estado + una pregunta + opciones, sale una probabilidad por opción en una sola pasada, sin generar texto), ejecutados en local.

**Hardware objetivo:** Dell i9‑13900H, 32 GB RAM, NVIDIA de 8 GB VRAM, Windows 11.

**Punto de partida:** [altertable-ai/duckdb-decide_onnx](https://github.com/altertable-ai/duckdb-decide_onnx) (MIT, C++17, ONNX Runtime, solo CPU, un solo modelo).

> Investigación hecha el 18‑nov‑2026. Las cifras vienen de model cards, repositorios y PRs públicos, no de mediciones propias. La fase 0 existe precisamente para medir en tus datos.

## ✅ Decisiones tomadas (18‑nov‑2026, tras la fase 0)

- **Uso comercial.** Solo entran modelos Apache‑2.0, así que StartLux y openjev quedan fuera.
- **Familia de modelos: OneJev1.6.** El modelo por defecto es el **4B**; el **0.8B** (rápido) y el **9B** (máxima calidad) se pueden elegir desde SQL.

  | id en la extensión | GGUF | Tamaño | Acierto fase 0 (es) | Uso |
  |---|---|---|---|---|
  | `onejev-0.8b` | `mradermacher/OneJev1.6-0.8B-GGUF` · Q8_0 | 0.8 GB | 85.8 % | Volumen alto, latencia mínima |
  | **`onejev-4b`** (por defecto) | `mradermacher/OneJev1.6-4B-GGUF` · Q4_K_M | 2.7 GB | **96.0 %** | Equilibrio calidad/velocidad |
  | `onejev-9b` | `mradermacher/OneJev1.6-9B-GGUF` · Q4_K_M | 5.6 GB | 100 % en los 95 anuncios que alcanzó a evaluar (el 4B: 94.7 % en esos mismos) | Máxima calidad; cabe en 8 GB de VRAM |

  Referencias medidas en el mismo set (226 textos): Qwen3.5‑4B sin ajustar 90.7 %, vela3‑fast‑ml 62.4 % y Qwen3.5‑0.8B sin ajustar 51.8 %. Detalle completo en [eval/RESULTS.md](eval/RESULTS.md).
- **Umbral de confianza:** 7 de los 9 errores del 4B tienen una confianza ≤ 0.62. Conviene exponer `confidence` y recomendar revisión manual por debajo de ~0.65.
- **Un solo backend: llama.cpp** (CUDA, con CPU como alternativa). Los tres tamaños comparten arquitectura (qwen35), tokenizador, plantilla de chat, prompt (`qev-labels-v2`) y lectura (logits de las letras de opción). Por eso cambiar de modelo es solo cambiar de archivo GGUF.
  - La fase 0 comprobó esta lectura con llama.cpp b12521: funciona.
  - Se quita ONNX Runtime del núcleo de la extensión, lo que simplifica mucho la compilación en Windows. Se conserva la interfaz SQL de `decide_onnx` (`decision_choose`, `decision_yes_no`, `DECIDE`).
- **DuckDB 2.0.0 ("Cyanoptera")** es la versión objetivo, a petición del usuario. Los submódulos están fijados así: `duckdb` @ v2.0.0 (d54cbbe3), `extension-ci-tools` @ rama `v2.0-cyanoptera` y `third_party/llama.cpp` @ b12521.
  - Una extensión cargable solo funciona con la misma versión de DuckDB: hay que usar DuckDB CLI 2.0.0, no la 1.5.5 instalada con winget, o el `duckdb.exe` que genera la compilación.
  - Cambio de API de DuckDB 2.0 que afecta a la extensión: las funciones dentro de un `FunctionSet` son inmutables, así que propiedades como `SetVolatile()` se fijan antes de `AddFunction`.
- **Requisito de versión:** llama.cpp **≥ b12521** o equivalente. El GGUF de OneJev declara una capa MTP (`nextn_predict_layers=1`) que llama.cpp antiguo (por ejemplo, llama‑cpp‑python 0.3.36) no sabe cargar.

## ⚡ Rendimiento: lo medido y lo que hay que implementar (18‑nov‑2026)

### Mediciones (máquina de desarrollo: i3‑8100 + Quadro P600 2 GB; en la laptop los valores absolutos serán mayores)

| Medición | Resultado | Consecuencia |
|---|---|---|
| Composición del prompt | Plantilla + sistema: **45** tokens fijos. Pregunta + opciones: **86–259**. Texto del usuario: **7–25** tokens de media | El texto es solo el **4–14 %** del cómputo; el resto se repite en cada fila |
| Pregunta primero (para cachear la parte repetida) | OneJev‑0.8B: **85.8 % → 45.1 %** | ❌ No sirve sin reentrenar. El modelo depende del formato `qev-labels-v2` (texto primero) |
| GPU frente a CPU (0.8B, procesado de prompt) | 87 tok/s en CPU frente a 964 tok/s en GPU | La GPU es obligatoria para volumen |
| Secuencias en paralelo (0.8B, 300 tokens cada una) | 1 → **307 tok/s**; 4 → 800; 8 → **868** | **~2.8×** al procesar varias filas por lote |
| Flash attention con 16–32 secuencias | Sin FA: cae a **137–312 tok/s**. Con FA: **840 tok/s** con 32 secuencias | FA activado siempre |

### Estado de la fase 2 (18‑nov‑2026)

Hecho y verificado:
- `decision_choose` (opciones como MAP nombre→descripción o como lista) y `decision_yes_no`. Ambas aceptan un 4.º argumento opcional con el modelo.
- `decide_prompt`, `decide_stats()` y `decide_clear_cache()`, más 7 ajustes de motor.
- **Paridad:** los 226 prompts son idénticos byte a byte a los de `llama-server`/Jinja. Las 226 predicciones coinciden con el harness Python y la precisión es la misma (85.8 % con el 0.8B). La diferencia media de probabilidad es 0.005 (extensión en CPU frente a harness en GPU).
- **Rendimiento en CPU** (i3, 0.8B): 140 tok/s con lotes de 16 secuencias, frente a 87 tok/s con una sola secuencia (llama-bench).
- **Deduplicación** (60 filas con 20 textos únicos → 20 evaluaciones) y **caché LRU** (la segunda ejecución sale entera de la caché en 3 ms).

- **Cláusula `DECIDE`:** gramática PEG portada de `decide_onnx` (MIT) a DuckDB 2.0, más la forma de función `decision_choose(q AMONG (...) GIVEN ...)`.
  - `decide_state()` renderiza `GIVEN` con varias columnas igual que `json.dumps(indent=2, ensure_ascii=False)` de `qev`; verificado byte a byte contra Python.
  - Probada de punta a punta con el 0.8B, y con errores de sintaxis claros.

- **Descarga automática** vía `httpfs` con SHA‑256 propio, archivo temporal + renombrado atómico, sello `.verified` y `decide_download()`.
  - Probada en el **DuckDB 2.0.0 oficial**: descargó el 0.8B (812 MB) a una carpeta vacía y clasificó.
- **Build:** versión exacta `v2.0.0` (`DUCKDB_EXPLICIT_VERSION`) y sin la capacidad `local_extension_repository`, para que la extensión cargue en el CLI oficial y `httpfs` se instale del repositorio oficial.
  - Todas las funciones que pueden fallar están marcadas con `SetFallible()`, que DuckDB 2.0 exige.

Pendiente:
- Optimizaciones 4 y 5 de la lista de abajo: prefijo compartido y tokenización parcial.
- **Compilar y medir con CUDA en la laptop.** Aquí no se puede: CUDA 13 ya no soporta la Quadro P600 (Pascal).

### Optimizaciones obligatorias para las fases 2 y 3 (en orden de impacto)

Ya implementadas en la fase 2: 1 (lotes multisecuencia), 2 (logits solo en el último token), 3 (un único dueño del contexto/GPU), 6 (deduplicación y LRU), 7 (modelo residente), 8 (`decide_kv_cache`) y 9 (truncado con `decide_max_tokens`). Flash attention queda en `AUTO`.

1. **Lotes multisecuencia** (~2.8× medido). Cada `DataChunk` se reparte en `llama_batch` de 8–32 secuencias, con `n_batch` 4096, `n_ubatch` 512 y `flash_attn` activado. El tamaño del lote se ajusta solo según la VRAM libre.
2. **Calcular logits solo en el último token de cada secuencia** (`batch.logits[i]=1` únicamente ahí). El vocabulario tiene 248k tokens, así que calcularlo en las ~300 posiciones de cada prompt multiplicaría el costo de la cabeza de salida por 300.
3. **Una sola cola de inferencia por proceso.** Un hilo dueño del `llama_context` y de la GPU; los hilos de DuckDB solo preparan y tokenizan filas. Así se evita la contención entre hilos de DuckDB e hilos de ggml. En CPU, los hilos de ggml se igualan a los núcleos físicos.
4. **Cachear el prefijo común** (sistema y plantilla: 45 tokens, entre el 13 y el 28 % del prompt). Se calcula una vez en la secuencia 0 y se copia con `llama_memory_seq_cp` a las demás. ⚠️ Hay que verificar que `seq_cp` funcione con la memoria recurrente híbrida de Qwen3.5. Cuando una fila tiene varias preguntas (`DECIDE` con `CHOOSE` + `ASK`), también se comparte el texto, como hace `qev`.
5. **Tokenizar las partes fijas una sola vez por consulta**, en el bind, y por fila solo el texto. Necesita un test de igualdad en los bordes de los tokens BPE (la fase 1 ya demuestra la igualdad del tokenizador con `llama-server`).
6. **Deduplicar textos repetidos** dentro de la consulta y usar una caché LRU con clave (modelo, hash de la pregunta, texto). En anuncios y keywords los duplicados son habituales.
7. **Cargar el modelo una sola vez y mantenerlo en memoria.** Se carga al primer uso y no al hacer el bind, se comparte entre conexiones y al cambiar `decide_model` se libera el anterior.
8. **Caché KV en `q8_0`** para el 9B. Permite más secuencias en paralelo dentro de 8 GB; `n_ctx` = secuencias × longitud máxima.
9. **Truncar los textos largos** (`decide_max_tokens`, 512 por defecto) para que un texto anómalo no frene todo el lote.
10. **Opciones de compilación:** `GGML_NATIVE` (ya está), `-CudaArch` exacta de la GPU (89 para RTX 40xx), CUDA graphs y Release.

### Mejoras futuras (requieren entrenamiento)
- **LoRA de OneJev con la pregunta primero:** permitiría cachear la pregunta y las opciones (86–259 tokens) y procesar solo el texto, con una ganancia teórica de 5–10×. Se puede entrenar con `OmniJev/OneJev-Data` y la receta de `train/` del repo OneJev. Requiere una GPU de entrenamiento y validar que la precisión no cae.
- **Descripciones de opciones más cortas:** son la mayor parte del costo (anuncios: 259 tokens). Conviene medir cuánto se pierde con descripciones breves o solo con los nombres de las opciones.

> Las secciones 2–4 de abajo son el análisis original. Donde contradigan este bloque, manda este bloque.

---

## 1. Qué hace hoy `decide_onnx` (y qué le falta)

| Tiene | Le falta para tu caso |
|---|---|
| Funciones `decision_choose(state, question, options)` → `STRUCT(choice, confidence, probabilities)` y `decision_yes_no(state, question)` → P(sí) | **Un solo modelo** fijo (vela3‑fast‑multilingual), sin forma de cambiarlo |
| Cláusula SQL `FROM t DECIDE (CHOOSE … AMONG (…) AS x, ASK … AS y GIVEN col)` | **Sin GPU**: todo va por CPU con ONNX Runtime |
| Descarga y caché del modelo con verificación SHA‑256, caché LRU de 100k prompts | **Sin batching** en el grafo: el paralelismo sale solo de los hilos de DuckDB |
| Tokenizador BPE propio en C++, empaquetado de prompts cortos | **Windows**: no compila con MinGW; con MSVC no está documentado (hay que probarlo) |
| Modelo multilingüe EN/FR/DE/**ES**/IT, Apache‑2.0 | Calidad en español modesta (ver §2) |

Dato útil: en el [PR #3](https://github.com/altertable-ai/duckdb-decide_onnx/pull/3) (cerrado sin mergear) los autores ya probaron a meter **OneJev1.6‑0.8B** (decoder Qwen3.5) por ONNX. Les dio **94 % frente a 72 %** en tickets FR/DE/ES/JA/ZH y mucha mejor calibración (ECE 0.035 frente a 0.121). A cambio era unas 2× más lento en CPU y les obligó a escribir kernels propios de Gated DeltaNet. **Conclusión: los decoders tipo Qwen3.5 son claramente mejores en español, pero meterlos por ONNX/CPU es caro. Para ellos conviene llama.cpp + CUDA.**

---

## 2. Análisis de modelos

### 2.1 Candidatos que caben en 8 GB de VRAM

| Modelo | Tamaño | Arquitectura / base | Español | Calidad (Decision Index 0.3, public) | Licencia | Formato | Encaje |
|---|---|---|---|---|---|---|---|
| **vela3‑fast‑multilingual** (Altertable) | 0.4B | Encoder UltraBERT‑380M | **Sí, explícito** (XNLI/PAWS‑X/Belebele ES: 62.7 → 67.5) | ≈52 (EN) | Apache‑2.0 | **ONNX int8 listo** | Backend CPU rápido; ya funciona en `decide_onnx` |
| **OneJev1.6‑0.8B** (OmniJev) | 0.9B | Qwen3.5‑0.8B | **Fuerte en la práctica** (94 % en tickets multilingües con ES, PR #3); el card no lista idiomas | 48.8 (mejor ≤2.5B) | **Apache‑2.0** | GGUF | Ligero, buena calibración |
| **OneJev1.6‑4B / 9B** (OmniJev) | 5B / 9.4B | Qwen3.5‑4B / 9B | Base multilingüe; no evaluado en ES | 9B ≈ 65 (sin confirmar) | **Apache‑2.0** | GGUF (comunidad) | **Candidato principal en GPU** |
| **StartLux‑Decision v1.1 4B / 9B** | 4B / 9B | No declarada | No declarado | **67.9 / 70.0** (los mejores de su tamaño) | **CC BY‑NC 4.0** (no comercial) | GGUF, compatible con llama.cpp | El mejor en calidad si el uso **no** es comercial |
| **pplx‑decider‑flash v1.2/1.3** (Perplexity) | ~4–5B | Qwen3.5‑4B + cabeza de lectura | No confirmado | — | Apache‑2.0 (probable) | GGUF oficial Q8 (5.2 GB) | ⚠️ Necesita un runner **no causal** propio: llama.cpp normal no sirve |
| **GLiNER2.5‑multi‑Decide** (Fastino) | 287M | mDeBERTa‑v3 | Multilingüe, sin evaluación ES publicada | — | Apache‑2.0 | PyTorch (exportable a ONNX) | Alternativa CPU ultrarrápida para etiquetas simples |
| **Qwen3.5‑4B / 9B instruct** (sin ajustar) | 4B / 9B | — | **Muy bueno en ES** | Paper "Inner Jev": los LLM abiertos sin ajuste ya rinden cerca de Jev | Apache‑2.0 | GGUF | **Baseline obligatorio** (lectura de logits de letras) |

### 2.2 Descartados

- **OpenJev (openjev/openjev)**: 27B (Q4 ≈16.5 GB, no cabe), CC BY‑NC, y no lista español.
- **OpenJev (alanhuangyoo)**: solo inglés.
- **OpenJev (zhihz)**: inglés y chino.
- Hay al menos 5 proyectos distintos llamados "OpenJev"; ninguno encaja.
- **H2O‑Lightning‑4B**: más del 99 % de su ajuste fino fue en inglés.
- **Todos los 27B–31B** (top del leaderboard: karpo, GLiDE, OneJev1.6g‑31B, Clef, Mammoth, pplx‑decider 27B): no caben en 8 GB. Con offload a RAM serían demasiado lentos para 10k filas.
- **vela3‑fast (inglés)**: su precisión en español cae a 0.44–0.54 en MASSIVE.
- **Liquid d1‑3B**: licencia con tope de ingresos y Decision Index de solo 38.9.
- **Strands Decider 2B**: Decision Index de solo 21.3.

### 2.3 Veredicto

1. **Modelo principal (GPU): OneJev1.6‑4B en Q8_0** (~5 GB). Si en la fase 0 gana con claridad, **OneJev1.6‑9B en Q4_K_M** (~5.6 GB).
   - Apache‑2.0, base Qwen3.5 (multilingüe) y la mejor evidencia de rendimiento en español entre los modelos de decisión abiertos.
2. **Si el uso es no comercial: StartLux‑Decision v1.1 9B Instant (Q4_K_M)**. Es el de mayor Decision Index que cabe en tu GPU.
3. **Backend CPU / rápido: vela3‑fast‑multilingual** (lo que ya trae `decide_onnx`). Sirve para preguntas fáciles, para máquinas sin GPU y como referencia.
4. **Baseline siempre presente: Qwen3.5‑9B instruct** con lectura de logits sobre las letras de las opciones (receta del paper ["Releasing the Inner Jev"](https://arxiv.org/abs/2610.17205)). Si un modelo de decisión no supera a este baseline en tus datos, no merece la pena.

La decisión final se toma con números en la **fase 0**, no antes. Ningún modelo de decisión publica una evaluación en español comparable.

---

## 3. Arquitectura propuesta

```
            SQL: decision_choose / decision_yes_no / DECIDE (…)  [+ parámetro model]
                                   │
                    ┌──────────────┴──────────────┐
                    │  Planificador por chunk     │  agrupa filas del DataChunk (2048),
                    │  (dedupe + caché LRU)       │  dedupe de prompts, caché de resultados
                    └──────────────┬──────────────┘
                                   │  IDecisionBackend::decide_batch(requests)
          ┌────────────────────────┼─────────────────────────┐
          ▼                        ▼                         ▼
  OnnxEncoderBackend        LlamaCppBackend             (futuro) HttpBackend
  vela3-fast-ml, GLiNER     GGUF + CUDA (8 GB)          servidor /v1/systemone local
  CPU, ONNX Runtime         OneJev, StartLux, Qwen      (OpenJev, StartLux engine…)
                                   │
                         ModelAdapter (JSON por modelo):
                         plantilla de prompt, tipo de lectura,
                         tokens de opciones, cabeza opcional
```

### 3.1 Abstracción clave: `ModelAdapter`

Cada modelo de decisión arma el prompt y lee el resultado a su manera. Para no meter `if (modelo == …)` en todo el código, cada modelo se describe con un archivo `models/<id>.json`:

```json
{
  "id": "onejev1.6-4b",
  "backend": "llamacpp",
  "file": "OneJev1.6-4B.Q8_0.gguf",
  "url": "https://huggingface.co/…/resolve/main/…gguf",
  "sha256": "…",
  "template": "systemone_v1",        // plantilla en C++ registrada por nombre
  "readout": "option_letter_logits", // | "hidden_state_head" | "encoder_marker_head"
  "head_file": null,                 // .npz/.bin si la lectura usa una cabeza propia
  "max_options": 16,
  "context": 16384,
  "license": "Apache-2.0"
}
```

Tipos de lectura que hay que implementar:

- **`option_letter_logits`**: una sola pasada y softmax sobre los logits de los tokens `A`, `B`, `C`… en la última posición. Sirve para Qwen3.5 a secas, la receta Inner Jev y probablemente OneJev/StartLux en modo Instant. **Hay que verificarlo en la fase 0 contra su runtime oficial.**
- **`hidden_state_head`**: se saca el embedding del último token (llama.cpp `embeddings=true`, `pooling=last`) y se le aplica en C++ una cabeza lineal pequeña que se carga aparte. Sirve para modelos con cabeza de decisión separada.
- **`encoder_marker_head`**: la lectura que ya trae `decide_onnx` para vela3 (puntuación en los marcadores `[O]`).

### 3.2 Backend llama.cpp (el que usa tu GPU)

- Se enlaza llama.cpp como submódulo, compilado con `GGML_CUDA=ON` y todas las capas en GPU (`n_gpu_layers = -1`).
- **Batching real:** cada `DataChunk` de DuckDB (hasta 2048 filas) se convierte en un `llama_batch` con varias secuencias (`n_seq_max` ≈ 32–64). Solo hace falta el logit de la última posición de cada secuencia, así que no se genera nada.
- **Reutilizar el prefijo:** la plantilla se ordena como `[sistema][pregunta + opciones][estado]`. El prefijo es idéntico en las 10k filas, así que se calcula una vez y se copia en caché KV (`llama_memory_seq_cp`). En clasificación masiva esto ahorra entre el 30 y el 60 % del cómputo (el PR #3 midió −38 % con una caché parecida).
- Un único `llama_context` por proceso, protegido por mutex o con una cola. La GPU es una sola, así que los hilos de DuckDB solo preparan los lotes.
- **Estimación (sin medir):** con un 4B Q8 en una GPU de portátil de 8 GB, el procesado de prompt ronda las 3–6k tok/s. 10k textos × ~300 tokens son unos 3M tokens, o sea **10–20 min**, y menos con la caché de prefijo.

### 3.3 API SQL (compatible con `decide_onnx`, más el modelo)

```sql
INSTALL 'decide.duckdb_extension';  LOAD decide;   -- sin firmar: duckdb -unsigned

-- modelos registrados y su estado en caché
SELECT * FROM decide_models();

-- modelo por defecto de la sesión ('onejev-4b' si no se cambia)
SET decide_model = 'onejev-9b';       -- 'onejev-0.8b' | 'onejev-4b' | 'onejev-9b'
SET decide_device = 'cuda';           -- 'cpu' | 'cuda'
SET decide_gpu_layers = -1;           -- -1 = todas las capas en GPU

-- clasificación de 10k textos
CREATE TABLE resultado AS
SELECT id,
       decision_choose(texto,
         'Clasifica el motivo principal del mensaje',
         {facturacion: 'Problemas con cobros o facturas',
          soporte:     'Fallos técnicos del producto',
          baja:        'Quiere cancelar el servicio',
          otro:        'Ninguna de las anteriores'}
       ) AS d,
       decision_yes_no(texto, '¿El cliente está enfadado?', model := 'onejev-0.8b') AS p_enfado
FROM read_parquet('textos.parquet');

-- comparar tamaños sobre la misma muestra
SELECT m, avg((decision_choose(texto, q, opts, model := m)).choice = etiqueta) AS acc
FROM muestra_etiquetada, (VALUES ('onejev-0.8b'), ('onejev-4b'), ('onejev-9b')) t(m)
GROUP BY m;
```

Recomendación de prompting (los model cards lo coinciden): **pregunta e instrucciones en español o en inglés, y opciones siempre con descripción**. vela3 rinde mejor con la pregunta en inglés; los decoders Qwen3.5 funcionan bien en español. Esto también se mide en la fase 0.

---

## 4. Plan de implementación por fases

### Fase 0: evaluación en Python (1–2 días). **Decide el modelo.**
1. Sacar 300 textos de tus 10k y etiquetarlos a mano, estratificados por clase. Es el conjunto de verdad.
2. Añadir referencias públicas en español: MASSIVE `es-ES` (intents), XNLI‑es y PAWS‑X‑es.
3. Script `eval/run.py` que, para cada candidato, calcule **accuracy, macro‑F1, ECE (calibración), filas/s y VRAM pico**, y guarde todo en `eval/results.duckdb`. Candidatos:
   - vela3‑fast‑multilingual (ONNX, `vela_onnx.py` del repo HF)
   - GLiNER2.5‑multi‑Decide
   - OneJev1.6‑0.8B / 4B / 9B (runtime oficial *y* llama‑cpp‑python con lectura `option_letter_logits`)
   - StartLux‑Decision v1.1 4B / 9B (si la licencia no comercial es aceptable)
   - Qwen3.5‑4B / 9B instruct (lectura por letras, receta Inner Jev)
4. **Punto crítico:** comprobar que la lectura con llama.cpp (logits de letras o embedding + cabeza) da las **mismas probabilidades** (±1e‑3) que el runtime oficial de cada modelo. Si un modelo no se puede reproducir con llama.cpp estándar (como pplx‑decider v1.2), se descarta o se manda al backend HTTP.
5. **Salida:** tabla comparativa, modelo por defecto elegido y archivos `models/*.json` validados.

### Fase 1: esqueleto de la extensión + compilación en Windows (2–3 días)
1. Partir de la plantilla oficial `duckdb/extension-template`, nombre `decide`. De `duckdb-decide_onnx` (MIT) se reutiliza la interfaz SQL y la gramática `DECIDE`, pero no ONNX Runtime.
2. Añadir llama.cpp (≥ b12521) como submódulo, con la opción CMake `DECIDE_WITH_CUDA`.
3. Compilar con **MSVC + Ninja** (Developer PowerShell de VS 2022) y CUDA Toolkit 12.x. Compilar también una variante solo CPU.
4. Hacer el "hola mundo": `SELECT decide_tokenize('hola')` cargando `onejev-0.8b`.

### Fase 2: núcleo de decisión (3–4 días)
1. **Registro de los 3 modelos** (`onejev-0.8b`, `onejev-4b` por defecto, `onejev-9b`), con URL, cuantización y SHA‑256 fijados en el código. Se descargan bajo demanda a `~/.duckdb/decide/<modelo>/`.
2. Ajustes SQL: `SET decide_model`, `decide_device`, `decide_gpu_layers`, `decide_model_path` (para usar sin conexión) y el parámetro con nombre `model :=`.
3. Función `decide_models()` que muestre id, tamaño, licencia, si está descargado y si está cargado.
4. **Prompt `qev-labels-v2` en C++**: plantilla de chat Qwen con el razonamiento desactivado, `<state>…</state>`, `Question/Options/Answer with one letter`. Tests de igualdad byte a byte contra `eval/run_eval.py`.
5. **Lectura:** softmax de los logits de las letras A…Z en la primera posición generada. Para pregunta sí/no, `yes`=A y `no`=B.
6. Funciones `decision_choose`, `decision_yes_no` y la cláusula `DECIDE`, compatibles con `decide_onnx`.
7. Cada modelo se carga una sola vez por proceso y se comparte entre conexiones. Al cambiar `decide_model` se carga el nuevo modelo y se libera el anterior.

### Fase 3: rendimiento con la GPU (2–4 días)
1. Batch multisecuencia: el `DataChunk` entero (hasta 2048 filas) se reparte en `llama_batch` de N secuencias, y solo se calculan los logits de la última posición.
2. Deduplicación de textos repetidos dentro del chunk, más la caché LRU de resultados (clave: modelo + prompt).
3. Si no hay GPU, caer a CPU de forma automática y avisar con un warning.
4. Nota: el estado recurrente de Qwen3.5 (Gated DeltaNet) no permite recortar la caché KV a mitad de secuencia. Por eso la caché de prefijo se hace con checkpoints, como en `llama-server`, o se omite.

### Fase 4: calidad, rendimiento y empaquetado (2–3 días)
1. **Tests de paridad:** fixtures con las probabilidades esperadas de la fase 0 para cada modelo (tolerancia 1e‑3).
2. **Benchmark:** 10k textos reales con cada modelo; anotar tiempo total, filas/s y VRAM.
3. **Calibración:** función `decide_calibrate(tabla_etiquetada)` que ajuste un *temperature scaling* por modelo y pregunta, y guarde la temperatura. Útil para umbrales del tipo "si confidence < 0.6 → revisión manual".
4. Opción `none_threshold` para la salida "ninguna opción encaja" (al estilo de StartLux).
5. Documentar en el README la instalación local (`duckdb -unsigned`, `LOAD 'ruta/decide.duckdb_extension'`), los modelos y sus licencias.

### Fase 5 (opcional): mejoras
- **Ajuste fino LoRA** de OneJev1.6‑4B o Qwen3.5‑4B con tus etiquetas (300–2000 ejemplos), siguiendo la receta de "Releasing the Inner Jev" (cabeza de decisión inicializada desde el LM head), y exportarlo a GGUF. Es lo que más precisión suele dar en un dominio concreto.
- `HttpBackend` para usar desde SQL servidores `/v1/systemone` (OpenJev, el engine de StartLux, pplx‑decider) cuando un modelo no pueda correr en llama.cpp.
- Cláusula `DECIDE … USING MODEL 'x'` en la gramática PEG.

**Esfuerzo total estimado:** 2–3 semanas a tiempo parcial. La fase 1 ya te da algo usable el primer día.

---

## 5. Riesgos

| Riesgo | Mitigación |
|---|---|
| La lectura oficial de OneJev/StartLux no es "logits de letras" (usa cabeza propia o atención bidireccional) | La fase 0 lo verifica; `hidden_state_head` cubre las cabezas lineales; si hay atención no causal, el modelo va al `HttpBackend` |
| No hay evaluaciones en español publicadas | Tu conjunto etiquetado de 300 textos es el árbitro; Qwen3.5 a secas es el seguro |
| Licencias CC BY‑NC (StartLux, openjev) | El registro muestra la licencia en `decide_models()`; por defecto solo modelos Apache‑2.0 |
| Compilar en Windows (ONNX Runtime + CUDA + DuckDB) | Usar MSVC (no MinGW) y fijar versiones; plan B: compilar y usar desde WSL2 con CUDA |
| Los GGUF de OneJev 4B/9B son conversiones de la comunidad | Verificar la paridad en la fase 0; si falla, convertir uno mismo con `convert_hf_to_gguf.py` |
| Textos largos | Truncado configurable (`decide_max_tokens`) y aviso con `decide_tokenize()` |

---

## 6. Fuentes

- [altertable-ai/duckdb-decide_onnx](https://github.com/altertable-ai/duckdb-decide_onnx) · [PR #3 OneJev1.6‑0.8B](https://github.com/altertable-ai/duckdb-decide_onnx/pull/3)
- [altertable/vela3-fast-multilingual-onnx-int8](https://huggingface.co/altertable/vela3-fast-multilingual-onnx-int8) · [vllm-sr/vela3-fast](https://huggingface.co/vllm-sr/vela3-fast) · [Vela 3.0 blog](https://vllm-sr.ai/blog/vela-3-0-name-it-vela-finds-it/)
- [OmniJev/OneJev1.6-0.8B](https://huggingface.co/OmniJev/OneJev1.6-0.8B) · [OneJev1.6-9B](https://huggingface.co/OmniJev/OneJev1.6-9B) · [OmniJev en GitHub](https://github.com/OmniJev/OneJev)
- [StartLux-Decision](https://github.com/StartLuxLabs/Startlux-Decision)
- [openjev/openjev](https://huggingface.co/openjev/openjev) · [alanhuangyoo/OpenJev](https://github.com/alanhuangyoo/OpenJev) · [zhihz/openjev](https://github.com/zhihz/openjev)
- [Decision Index leaderboard](https://huggingface.co/datasets/multimodalart/decision-index-leaderboard)
- [Releasing the Inner Jev (arXiv 2610.17205)](https://arxiv.org/abs/2610.17205)
- [pplx-decider-flash GGUF oficial](https://huggingface.co/perplexity-ai/pplx-decider-flash-gguf-v1.2)
- [h2oai/h2o-lightning-4b](https://huggingface.co/h2oai/h2o-lightning-4b)
- [GLiNER2.5-Decide](https://innfactory.ai/en/ai-models/fastino-gliner-decide/)
- [7 Best AI Decision Models 2026 (pinggy)](https://pinggy.io/blog/best_ai_decision_models/)
