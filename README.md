# decide_amox

[![CI](https://github.com/DSandovalFlavio/desision_model_extension_duckdb/actions/workflows/MainDistributionPipeline.yml/badge.svg)](https://github.com/DSandovalFlavio/desision_model_extension_duckdb/actions/workflows/MainDistributionPipeline.yml)
[![Release](https://github.com/DSandovalFlavio/desision_model_extension_duckdb/actions/workflows/Release.yml/badge.svg)](https://github.com/DSandovalFlavio/desision_model_extension_duckdb/actions/workflows/Release.yml)
![DuckDB](https://img.shields.io/badge/DuckDB-2.0.0-yellow)
![License](https://img.shields.io/badge/license-MIT-blue)

**Clasifica texto desde SQL, en local y en español**, con modelos de decisión. Una extensión de DuckDB 2.0 para etiquetar anuncios, keywords, comentarios o tickets sin enviar datos a ninguna API.

```sql
SELECT texto, sentimiento, sentimiento_confidence
FROM comentarios DECIDE (
    CHOOSE '¿Qué sentimiento expresa este comentario sobre la marca?' AMONG (
        positivo 'Opinión favorable: satisfacción, elogio o recomendación',
        negativo 'Queja, enojo, decepción o crítica (incluye sarcasmo)',
        neutro   'Pregunta, dato o comentario informativo',
        mixto    'Combina claramente aspectos positivos y negativos'
    ) AS sentimiento
    GIVEN texto
);
```

- **Local y privado:** el modelo corre en tu equipo, con GPU NVIDIA o con CPU. Los textos nunca salen de tu máquina.
- **Una sola pasada por decisión, sin generar texto:** el modelo devuelve una probabilidad por opción y una confianza que sirve para mandar a revisión los casos dudosos.
- **Modelos OneJev1.6** (Apache‑2.0, licencia comercial) en tres tamaños. En nuestro set de marketing en español, el 4B acertó el **96 %** ([evaluación](docs/EVALUATION.md)).
- **Integridad verificada:** los modelos se descargan de Hugging Face y se comprueba su SHA‑256 antes de usarlos.

## Instalación

Necesitas el [DuckDB CLI **2.0.0**](https://github.com/duckdb/duckdb/releases/tag/v2.0.0). La extensión solo carga en esa versión exacta. No hace falta compilar nada.

### Windows con GPU NVIDIA (recomendado)

1. Descarga `decide_amox-<versión>-windows_amd64-cuda12.zip` desde [Releases](https://github.com/DSandovalFlavio/desision_model_extension_duckdb/releases).
   - Funciona con GTX 10xx, RTX 20xx, 30xx y 40xx, y RTX Ada.
   - Necesitas el driver de NVIDIA actualizado; **no** hace falta instalar el CUDA Toolkit.
2. Descomprime el zip y copia los tres `*.dll` (`cudart64_12`, `cublas64_12`, `cublasLt64_12`) **en la misma carpeta que `duckdb.exe`**. También puedes añadir la carpeta del zip al `PATH`.
3. Abre DuckDB permitiendo extensiones sin firma y cárgala:
   ```powershell
   duckdb -unsigned
   ```
   ```sql
   LOAD 'C:/ruta/al/zip/decide_amox.duckdb_extension';
   SELECT amox_version();   -- debe listar CUDA en "backends"
   ```

### Sin GPU NVIDIA (Windows, Linux, macOS)

- **Pronto, firmada por DuckDB y sin `-unsigned`:**
  ```sql
  INSTALL decide_amox FROM community;
  LOAD decide_amox;
  ```
  Está pendiente de aceptación en *DuckDB Community Extensions*; ver [docs/PUBLISHING.md](docs/PUBLISHING.md). Esta versión usa CPU en Windows y Linux, y la GPU de Apple (Metal) en macOS.
- **Mientras tanto, en Windows:** usa `decide_amox-<versión>-windows_amd64-cpu.zip` de [Releases](https://github.com/DSandovalFlavio/desision_model_extension_duckdb/releases), con los mismos pasos 2–3 pero sin DLL.

### Configurarla una sola vez

Crea `C:\Users\<usuario>\.duckdbrc` (DuckDB lo ejecuta al iniciar):

```sql
LOAD 'C:/decide_amox/decide_amox.duckdb_extension';
```

Para no escribir `-unsigned` cada vez, crea un acceso directo `duckdb-amox.bat` con el contenido `duckdb.exe -unsigned %*`.

### ¿Por qué `-unsigned`?

DuckDB solo firma las extensiones que compila su propio CI, y ese CI no tiene CUDA. La versión con GPU la compila y publica automáticamente **el CI de este repositorio** a partir del código que ves aquí. Puedes verificar cada zip con el `.sha256` que lo acompaña en el release.

## Primeros pasos

```sql
-- 1) Descargar el modelo por defecto (onejev-4b, 2.7 GB). También se descarga solo en la primera consulta.
SELECT amox_download();

-- 2) Elegir una opción: MAP nombre -> descripción
SELECT texto,
       amox_choose(texto, '¿A qué sector pertenece este anuncio?', MAP {
           'moda':     'Ropa, calzado o accesorios',
           'viajes':   'Vuelos, hoteles o paquetes turísticos',
           'finanzas': 'Bancos, tarjetas, préstamos o seguros'}) AS r
FROM anuncios;
-- r.choice, r.confidence, r.probabilities['viajes']

-- 3) Pregunta de sí/no: probabilidad de "sí"
SELECT anuncio, amox_yes_no(anuncio, '¿Este anuncio infringe las políticas de Google Ads?') AS p
FROM anuncios;

-- 4) Varias preguntas a la vez y varias columnas como contexto
SELECT * FROM tickets DECIDE (
    CHOOSE '¿Qué equipo debe atenderlo?' AMONG (facturacion, soporte, ventas) AS equipo,
    ASK '¿El cliente pide un reembolso?' AS reembolso
    GIVEN asunto, mensaje
);
```

Referencia completa de funciones, cláusula y ajustes: **[docs/REFERENCE.md](docs/REFERENCE.md)**.

## Modelos

| Modelo | Descarga | Acierto (set de marketing en español) | Úsalo para |
|---|---|---|---|
| `onejev-0.8b` | 0.8 GB | 85.8 % | Mucho volumen, CPU o GPU pequeña |
| **`onejev-4b`** (por defecto) | 2.7 GB | **96.0 %** | Uso general |
| `onejev-9b` | 5.6 GB | 100 % en los 95 anuncios evaluados | Máxima calidad (GPU de 8 GB) |

```sql
SET amox_model = 'onejev-9b';                                  -- para la sesión
SELECT amox_choose(texto, 'pregunta', ['a', 'b'], 'onejev-0.8b') FROM t;   -- para una consulta
SELECT * FROM amox_models();                                   -- descargados, cargado, URL, SHA-256
```

- **Dónde se guardan:** por defecto en `~/.duckdb/decide_amox/models`. Se puede cambiar con `SET amox_model_dir`.
- **Sin acceso a Hugging Face** (por ejemplo, en la red del trabajo): copia los `.gguf` por Drive o USB y sigue **[docs/OFFLINE.md](docs/OFFLINE.md)**.

## Rendimiento

- **La GPU marca la diferencia.** En nuestras pruebas fue unas 10 veces más rápida que la CPU con el mismo modelo.
- **Descripciones de opciones cortas.** La pregunta y las opciones se procesan en cada fila y suelen pesar más que el texto.
- **Textos repetidos y consultas repetidas son gratis:** se deduplican y se guardan en una caché.
- **Medición:** `SELECT * FROM amox_stats();` muestra tokens/s, decisiones/s, aciertos de caché y el modelo cargado.
- **Errores de memoria en GPU:** `SET amox_parallel = 8; SET amox_kv_cache = 'q8_0';`.

## Documentación

| Documento | Contenido |
|---|---|
| [docs/REFERENCE.md](docs/REFERENCE.md) | Funciones, cláusula `DECIDE`, ajustes y salidas |
| [docs/OFFLINE.md](docs/OFFLINE.md) | Repartir modelos y extensión sin internet |
| [docs/EVALUATION.md](docs/EVALUATION.md) | Cómo se eligió el modelo y resultados |
| [docs/BUILDING.md](docs/BUILDING.md) | Compilar desde el código, CI y cómo publicar un release |
| [docs/PUBLISHING.md](docs/PUBLISHING.md) | Publicación en DuckDB Community Extensions |
| [docs/DESIGN.md](docs/DESIGN.md) | Investigación, decisiones de diseño y mediciones |
| [CHANGELOG.md](CHANGELOG.md) · [SECURITY.md](SECURITY.md) | Versiones y seguridad |

## Estado

Versión **0.1.0**, la primera publicada.

- **Comprobado:**
  - El CI compila y pasa las pruebas en Linux (amd64 y arm64), macOS (arm64) y Windows (amd64).
  - El zip de Windows generado por el CI se probó con el DuckDB 2.0.0 oficial.
  - Hay paridad exacta frente a la implementación de referencia de OneJev: prompts idénticos y mismas predicciones.
- **Estado actual de cada build:** en los badges de arriba.

## Licencias

- **decide_amox:** MIT ([LICENSE](LICENSE)).
- **Modelos OneJev1.6:** Apache‑2.0 (OmniJev). Los GGUF son de mradermacher.
- **Incluye** llama.cpp (MIT), la gramática `DECIDE` adaptada de duckdb-decide_onnx (MIT) y el formato de prompt de OneJev (Apache‑2.0). Detalles en [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- **Los zips con CUDA** incluyen DLL redistribuibles de NVIDIA (licencia NVIDIA CUDA EULA).
