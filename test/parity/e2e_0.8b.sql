-- Clasificación end-to-end desde SQL con la extensión, sobre el set de la fase 0.
LOAD 'build/release/extension/decide/decide.duckdb_extension';
SET decide_model_dir = 'models';
SET decide_model = 'onejev-0.8b';
.timer on
CREATE TEMP TABLE e AS SELECT * FROM read_json('test/parity/expected_prompts.jsonl', format='newline_delimited');
CREATE TABLE ext_preds AS
SELECT id, task, kind,
       CASE WHEN kind = 'noul' THEN NULL
            ELSE decision_choose(text, question, map_from_entries(list_zip(labels, descs)::STRUCT(k VARCHAR, v VARCHAR)[])) END AS d,
       CASE WHEN kind = 'noul' THEN decision_yes_no(text, question) END AS p_yes
FROM e;
.timer off
SELECT * FROM decide_stats();
COPY (SELECT id, task, coalesce(d.choice, CASE WHEN p_yes >= 0.5 THEN 'yes' ELSE 'no' END) AS pred,
             coalesce(d.confidence, greatest(p_yes, 1 - p_yes)) AS confidence,
             coalesce(d.probabilities, MAP {'yes': p_yes, 'no': 1 - p_yes}) AS probs
      FROM ext_preds) TO 'test/parity/ext_0.8b.json' (FORMAT json);
