# Evaluación: cómo se eligió el modelo

Se compararon modelos de decisión abiertos con licencia comercial, más Qwen3.5 sin ajustar como referencia, sobre **226 ejemplos sintéticos de marketing en español**:

| Tarea | Ejemplos | Clases |
|---|---|---|
| Sector de anuncios de Google Ads | 100 | 10 |
| Intención de búsqueda de keywords | 48 | 4 |
| Sentimiento de comentarios (con sarcasmo y casos mixtos) | 48 | 4 |
| ¿El anuncio infringe políticas? | 30 | sí/no |

| Modelo | Aciertos | Macro‑F1 (media de las 4 tareas) |
|---|---|---|
| **OneJev1.6‑4B** (por defecto) | **96.0 %** | **0.964** |
| Qwen3.5‑4B (sin ajustar) | 90.7 % | 0.881 |
| OneJev1.6‑0.8B | 85.8 % | 0.843 |
| vela3‑fast‑multilingual (el de duckdb-decide_onnx) | 62.4 % | 0.616 |
| Qwen3.5‑0.8B (sin ajustar) | 51.8 % | 0.453 |

OneJev1.6‑9B solo se evaluó en los primeros 95 anuncios: acertó el 100 %, frente al 94.7 % del 4B en esos mismos ejemplos.

**Cómo leer estas cifras:** el set es sintético y pequeño. Sirve para comparar modelos entre sí, pero no garantiza el mismo acierto con tus datos. Antes de usar la extensión en producción, etiqueta una muestra propia (200–300 filas) y mide.

- Detalle por tarea, errores y calibración: [eval/RESULTS.md](../eval/RESULTS.md).
- Investigación completa y decisiones: [DESIGN.md](DESIGN.md).

## Reproducirla

Requiere Python 3.11+ y `llama-server` b12521 ([releases de llama.cpp](https://github.com/ggml-org/llama.cpp/releases/tag/b12521)) en `tools/llama-b12521-cuda12/`, o la ruta que indiques con `--server`.

```powershell
python -m venv .venv
```

```powershell
.venv\Scripts\pip install jinja2 huggingface_hub onnxruntime tokenizers duckdb numpy
```

```powershell
.venv\Scripts\python eval\download_models.py
```

```bash
bash eval/run_all.sh
```

```powershell
.venv\Scripts\python eval\report.py
```

El harness (`eval/run_eval.py`) replica el formato de prompt y la lectura por letras de la extensión. `test/parity/` comprueba que ambos coinciden.
