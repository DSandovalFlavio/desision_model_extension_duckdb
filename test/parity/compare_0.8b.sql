-- Compara las predicciones de la extensión (C++, llama.cpp enlazado) con las del harness Python (llama-server).
-- Pequeñas diferencias numéricas son esperables si los backends difieren (CPU frente a CUDA).
ATTACH 'eval/results.duckdb' AS r (READ_ONLY);
CREATE TEMP TABLE ref AS
SELECT id, task, label, pred, confidence, probs::JSON AS probs
FROM r.predictions
WHERE model = 'onejev1.6-0.8b' AND prompt_lang = 'es' AND coalesce(layout, 'state_first') = 'state_first'
  AND coalesce(note, '') NOT LIKE 'parcial%'
QUALIFY row_number() OVER (PARTITION BY id ORDER BY ts DESC) = 1;
CREATE TEMP TABLE ext AS SELECT * FROM read_json('test/parity/ext_0.8b.json');

-- Diferencia máxima de probabilidad por ejemplo, sobre todas las opciones
CREATE TEMP TABLE diff AS
SELECT ref.id, ref.task, ref.label, ref.pred AS pred_ref, ext.pred AS pred_ext,
       max(abs((ref.probs ->> k.key)::DOUBLE - (to_json(ext.probs) ->> k.key)::DOUBLE)) AS max_abs_diff
FROM ref JOIN ext USING (id), LATERAL (SELECT unnest(json_keys(ref.probs)) AS key) k
GROUP BY ALL;

SELECT count(*) AS n,
       count(*) FILTER (WHERE pred_ref = pred_ext) AS misma_prediccion,
       round(max(max_abs_diff), 4) AS max_diff_prob,
       round(avg(max_abs_diff), 5) AS media_diff_prob,
       round(avg((pred_ext = label)::INT), 3) AS acc_extension,
       round(avg((pred_ref = label)::INT), 3) AS acc_harness
FROM diff;

SELECT * FROM diff WHERE pred_ref <> pred_ext ORDER BY max_abs_diff DESC LIMIT 10;
