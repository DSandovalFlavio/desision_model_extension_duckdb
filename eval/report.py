"""Informe de la fase 0: lee eval/results.duckdb y escribe eval/RESULTS.md (todo el cálculo es SQL en DuckDB)."""
from pathlib import Path

import duckdb

ROOT = Path(__file__).resolve().parent.parent
con = duckdb.connect(str(ROOT / "eval" / "results.duckdb"), read_only=True)

# Última corrida completa por (modelo, idioma)
con.execute("""
CREATE TEMP VIEW p AS
WITH last AS (
  SELECT model, prompt_lang, arg_max(run_id, ts) AS run_id
  FROM predictions GROUP BY ALL
)
SELECT p.* FROM predictions p JOIN last USING (model, prompt_lang, run_id)
WHERE coalesce(p.note, '') NOT LIKE 'parcial%'
""")

# Corridas parciales: se comparan con todos los modelos solo sobre los ids que alcanzaron a cubrir
partial = con.sql("""
WITH par AS (SELECT * FROM predictions WHERE note LIKE 'parcial%'),
ids AS (SELECT DISTINCT id FROM par),
allp AS (SELECT model, prompt_lang, id, pred, label FROM p UNION ALL SELECT model, prompt_lang, id, pred, label FROM par)
SELECT model, prompt_lang AS lang, count(*) AS n, round(avg((pred=label)::INT), 3) AS accuracy
FROM allp WHERE id IN (SELECT id FROM ids) GROUP BY ALL ORDER BY accuracy DESC, model
""")

# F1 por clase -> macro-F1 por tarea
con.execute("""
CREATE TEMP VIEW f1 AS
WITH classes AS (SELECT DISTINCT task, label AS c FROM p),
runs AS (SELECT DISTINCT model, prompt_lang FROM p),
cnt AS (
  SELECT r.model, r.prompt_lang, c.task, c.c,
         count(*) FILTER (WHERE p.pred = c.c AND p.label = c.c) AS tp,
         count(*) FILTER (WHERE p.pred = c.c AND p.label <> c.c) AS fp,
         count(*) FILTER (WHERE p.pred <> c.c AND p.label = c.c) AS fn
  FROM runs r CROSS JOIN classes c
  LEFT JOIN p ON p.model = r.model AND p.prompt_lang = r.prompt_lang AND p.task = c.task
  GROUP BY ALL
)
SELECT model, prompt_lang, task,
       avg(CASE WHEN 2*tp+fp+fn = 0 THEN 0 ELSE 2.0*tp/(2*tp+fp+fn) END) AS macro_f1
FROM cnt GROUP BY ALL
""")

# ECE con 10 bins sobre la confianza de la predicción
con.execute("""
CREATE TEMP VIEW ece AS
WITH b AS (
  SELECT model, prompt_lang, least(floor(confidence*10), 9) AS bin,
         count(*) AS n, avg(confidence) AS conf, avg((pred=label)::INT) AS acc
  FROM p GROUP BY ALL
)
SELECT model, prompt_lang, sum(n*abs(conf-acc))/sum(n) AS ece FROM b GROUP BY ALL
""")

summary = con.sql("""
SELECT p.model, p.prompt_lang AS lang, count(*) AS n,
       round(avg((pred=label)::INT), 3) AS accuracy,
       round((SELECT avg(macro_f1) FROM f1 WHERE f1.model=p.model AND f1.prompt_lang=p.prompt_lang), 3) AS macro_f1_tareas,
       round(any_value(e.ece), 3) AS ece,
       round(avg(power(1-p_label, 2)), 3) AS brier_label,
       round(median(latency_ms)) AS ms_mediana,
       round(sum(n_tokens)/(sum(latency_ms)/1000)) AS tok_s,
       any_value(ngl) AS ngl
FROM p JOIN ece e ON e.model=p.model AND e.prompt_lang=p.prompt_lang
GROUP BY p.model, p.prompt_lang
ORDER BY macro_f1_tareas DESC
""")

by_task = con.sql("""
SELECT model, prompt_lang AS lang,
  round(max(macro_f1) FILTER (WHERE task='ads_vertical'), 3) AS ads_vertical,
  round(max(macro_f1) FILTER (WHERE task='search_intent'), 3) AS search_intent,
  round(max(macro_f1) FILTER (WHERE task='sentiment'), 3) AS sentiment,
  round(max(macro_f1) FILTER (WHERE task='ad_policy'), 3) AS ad_policy
FROM f1 GROUP BY ALL ORDER BY ads_vertical + search_intent + sentiment + ad_policy DESC
""")

_rows = summary.limit(1).fetchall()
best = _rows[0] if _rows else None
best_model, best_lang = (best[0], best[1]) if best else (None, None)
errors = con.sql(f"""
SELECT task, id, label, pred, round(confidence, 2) AS conf
FROM p WHERE model='{best_model}' AND prompt_lang='{best_lang}' AND pred<>label ORDER BY task, id
""") if best else None
confusion = con.sql(f"""
SELECT label, pred, count(*) AS n FROM p
WHERE model='{best_model}' AND prompt_lang='{best_lang}' AND task IN ('search_intent','sentiment') AND pred<>label
GROUP BY ALL ORDER BY n DESC LIMIT 12
""") if best else None


def md(rel) -> str:
    cols = rel.columns
    rows = rel.fetchall()
    out = ["| " + " | ".join(cols) + " |", "|" + "---|" * len(cols)]
    out += ["| " + " | ".join("" if v is None else str(v) for v in r) + " |" for r in rows]
    return "\n".join(out)


text = f"""# Resultados fase 0 (es_marketing_v1)

Generado por `eval/report.py` desde `eval/results.duckdb`. 226 ejemplos en español: 100 anuncios de Google Ads
(10 verticales), 48 keywords (intención de búsqueda), 48 comentarios de marketing (sentimiento, con sarcasmo y
casos mixtos) y 30 anuncios para revisión de políticas (sí/no). `lang` = idioma de la pregunta y las opciones;
los textos siempre están en español.

- **accuracy**: aciertos sobre todos los ejemplos.
- **macro_f1_tareas**: media de la macro-F1 de las 4 tareas (métrica principal).
- **ece**: error de calibración (0 = la confianza coincide con la tasa de acierto).
- **brier_label**: media de (1 - p(etiqueta correcta))^2, menor es mejor.
- **tok_s / ms_mediana**: velocidad en la máquina de prueba (no en la laptop de destino).

## Resumen

{md(summary)}

## Macro-F1 por tarea

{md(by_task)}

## Corridas parciales (comparadas solo sobre los ejemplos que alcanzaron a cubrir)

{md(partial) if partial.limit(1).fetchall() else 'Ninguna.'}

## Errores del mejor ({best_model}, {best_lang})

{md(errors) if errors else ''}

### Confusiones más frecuentes (intención y sentimiento)

{md(confusion) if confusion else ''}
"""
(ROOT / "eval" / "RESULTS.md").write_text(text, encoding="utf-8")
print(text)
