-- Caché LRU y deduplicación: 20 textos, luego los mismos repetidos 3 veces, luego la consulta otra vez.
LOAD 'build/release/extension/decide/decide.duckdb_extension';
SET decide_model_dir = 'models';
SET decide_model = 'onejev-0.8b';
CREATE TEMP TABLE e AS SELECT * FROM read_json('test/parity/expected_prompts.jsonl', format='newline_delimited') WHERE kind = 'noul' LIMIT 20;
.timer on
-- 1) 60 filas = 20 textos x 3: debe evaluar 20 y deduplicar 40
SELECT count(*) AS filas, round(avg(p), 3) AS p_media FROM (SELECT decision_yes_no(text, question) AS p FROM e, range(3));
SELECT decisions, cache_hits, deduplicated, evaluated FROM decide_stats();
-- 2) la misma consulta otra vez: todo desde la caché
SELECT count(*) AS filas, round(avg(p), 3) AS p_media FROM (SELECT decision_yes_no(text, question) AS p FROM e, range(3));
SELECT decisions, cache_hits, deduplicated, evaluated FROM decide_stats();
