-- Paridad del prompt: decide_prompt() (C++) frente a llama-server /apply-template (plantilla Jinja oficial).
LOAD 'build/release/extension/decide/decide.duckdb_extension';
CREATE TEMP TABLE e AS SELECT * FROM read_json('test/parity/expected_prompts.jsonl', format='newline_delimited');
CREATE TEMP TABLE c AS
SELECT id, kind, prompt AS expected,
       CASE WHEN kind = 'noul' THEN decide_prompt(text, question)
            ELSE decide_prompt(text, question, map_from_entries(list_zip(labels, descs)::STRUCT(k VARCHAR, v VARCHAR)[])) END AS actual
FROM e;
SELECT count(*) AS total, count(*) FILTER (WHERE actual = expected) AS identicos FROM c;
SELECT id, expected, actual FROM c WHERE actual <> expected LIMIT 2;
