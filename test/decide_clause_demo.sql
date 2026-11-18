-- Demo de la cláusula DECIDE. Desde la raíz del repo:
--   build/release/duckdb.exe -unsigned -c ".read test/decide_clause_demo.sql"
LOAD 'build/release/extension/decide/decide.duckdb_extension';
SET decide_model_dir = 'models';
SET decide_model = 'onejev-0.8b';
.mode box

CREATE TEMP TABLE comentarios(id INT, canal VARCHAR, texto VARCHAR);
INSERT INTO comentarios VALUES
  (1, 'instagram', 'Me llegó el pedido en un día y la calidad es increíble, ya es mi tercera compra'),
  (2, 'twitter',   'Qué maravilla, otra vez sin internet todo el día. Gracias por nada'),
  (3, 'facebook',  '¿A qué hora abren los domingos?'),
  (4, 'google',    'La ropa es preciosa y de buena calidad, pero el envío tardó casi un mes');

-- 1) Cláusula completa: dos preguntas, un solo texto como estado
SELECT id, sentimiento, round(sentimiento_confidence, 2) AS conf, queja, round(queja_confidence, 2) AS conf_queja
FROM comentarios DECIDE (
    CHOOSE '¿Qué sentimiento expresa este comentario sobre la marca?' AMONG (
        positivo 'Opinión favorable: satisfacción, elogio o recomendación',
        negativo 'Queja, enojo, decepción o crítica (incluye sarcasmo)',
        neutro   'Pregunta, dato o comentario informativo',
        mixto    'Combina claramente aspectos positivos y negativos'
    ) AS sentimiento,
    ASK '¿El cliente se está quejando de algo?' AS queja
    GIVEN texto
)
ORDER BY id;

-- 2) Varias columnas en GIVEN: el estado es un JSON con sangría 2 (formato qev)
SELECT decide_state(struct_pack(canal := 'twitter', texto := 'Hola "mundo"')) AS estado;

-- 3) Sintaxis de función con AMONG / GIVEN
SELECT id, decision_choose('¿Qué sentimiento expresa?' AMONG (positivo, negativo, neutro, mixto) GIVEN canal, texto).choice AS s
FROM comentarios ORDER BY id;

-- 4) El reescrito no cuesta más: ambas preguntas pasaron por el mismo motor (lotes + caché)
SELECT decisions, cache_hits, deduplicated, evaluated FROM decide_stats();
