-- Prueba de humo de la fase 1. Desde la raíz del repo:
--   build/release/duckdb.exe -unsigned -c ".read test/smoke_phase1.sql"
.mode box
LOAD 'build/release/extension/decide/decide.duckdb_extension';
SELECT decide_version() AS version;

SELECT model, is_default, is_current, size, quant, round(file_mb) AS mb, license, downloaded
FROM decide_models() ORDER BY file_mb;

SET decide_model_dir = 'models';
SELECT model, downloaded, local_path FROM decide_models() ORDER BY file_mb;

-- Tokenizer con el modelo por defecto (4B) y con el 0.8B explícito: deben coincidir (mismo vocabulario)
SELECT decide_tokenize('Tenis Running Hasta 40% Off | Envío Gratis en 24h') AS tokens_4b;
SELECT decide_tokenize('Tenis Running Hasta 40% Off | Envío Gratis en 24h', 'onejev-0.8b') AS tokens_08b;
SELECT decide_tokenize('A') AS letra_a, decide_tokenize('B') AS letra_b, decide_tokenize('<|im_start|>') AS especial;

-- Vectorizado sobre el set de evaluación
SELECT task, count(*) AS n, round(avg(len(decide_tokenize(text))), 1) AS tokens_medios
FROM read_json_auto('eval/data/es_marketing_v1.jsonl') GROUP BY task ORDER BY task;

SELECT model, loaded FROM decide_models() ORDER BY file_mb;
