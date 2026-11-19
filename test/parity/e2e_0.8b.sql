-- Clasificación end-to-end desde SQL con la extensión, sobre el set de la fase 0.
LOAD 'build/release/extension/decide_amox/decide_amox.duckdb_extension';
SET amox_model_dir = 'models';
SET amox_model = 'onejev-0.8b';
.timer on
CREATE TEMP TABLE e AS SELECT * FROM read_json('test/parity/expected_prompts.jsonl', format='newline_delimited');
CREATE TABLE ext_preds AS
SELECT id, task, kind,
       CASE WHEN kind = 'noul' THEN NULL
            ELSE amox_choose(text, question, map_from_entries(list_zip(labels, descs)::STRUCT(k VARCHAR, v VARCHAR)[])) END AS d,
       CASE WHEN kind = 'noul' THEN amox_yes_no(text, question) END AS p_yes
FROM e;
.timer off
SELECT * FROM amox_stats();
COPY (SELECT id, task, coalesce(d.choice, CASE WHEN p_yes >= 0.5 THEN 'yes' ELSE 'no' END) AS pred,
             coalesce(d.confidence, greatest(p_yes, 1 - p_yes)) AS confidence,
             coalesce(d.probabilities, MAP {'yes': p_yes, 'no': 1 - p_yes}) AS probs
      FROM ext_preds) TO 'test/parity/ext_0.8b.json' (FORMAT json);
