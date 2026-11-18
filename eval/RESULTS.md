# Resultados fase 0 (es_marketing_v1)

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

| model | lang | n | accuracy | macro_f1_tareas | ece | brier_label | ms_mediana | tok_s | ngl |
|---|---|---|---|---|---|---|---|---|---|
| onejev1.6-4b | es | 226 | 0.96 | 0.964 | 0.068 | 0.046 | 3200.0 | 66.0 | 18 |
| qwen3.5-4b | es | 226 | 0.907 | 0.881 | 0.04 | 0.072 | 3810.0 | 50.0 | 18 |
| onejev1.6-0.8b | es | 226 | 0.858 | 0.843 | 0.065 | 0.129 | 491.0 | 447.0 | 99 |
| vela3-fast-ml | es | 226 | 0.624 | 0.616 | 0.07 | 0.317 | 1153.0 | 117.0 | 0 |
| qwen3.5-0.8b | es | 226 | 0.518 | 0.453 | 0.221 | 0.414 | 286.0 | 715.0 | 99 |

## Macro-F1 por tarea

| model | lang | ads_vertical | search_intent | sentiment | ad_policy |
|---|---|---|---|---|---|
| onejev1.6-4b | es | 0.951 | 0.958 | 0.979 | 0.967 |
| qwen3.5-4b | es | 0.939 | 0.916 | 0.918 | 0.753 |
| onejev1.6-0.8b | es | 0.879 | 0.876 | 0.825 | 0.792 |
| vela3-fast-ml | es | 0.596 | 0.44 | 0.833 | 0.597 |
| qwen3.5-0.8b | es | 0.58 | 0.6 | 0.297 | 0.333 |

## Corridas parciales (comparadas solo sobre los ejemplos que alcanzaron a cubrir)

| model | lang | n | accuracy |
|---|---|---|---|
| onejev1.6-9b | es | 95 | 1.0 |
| onejev1.6-4b | es | 95 | 0.947 |
| qwen3.5-4b | es | 95 | 0.937 |
| onejev1.6-0.8b | es | 95 | 0.884 |
| vela3-fast-ml | es | 95 | 0.611 |
| qwen3.5-0.8b | es | 95 | 0.547 |

## Errores del mejor (onejev1.6-4b, es)

| task | id | label | pred | conf |
|---|---|---|---|---|
| ad_policy | ap013 | yes | no | 0.51 |
| ads_vertical | av029 | finanzas | alimentos | 0.62 |
| ads_vertical | av070 | inmobiliaria | alimentos | 0.46 |
| ads_vertical | av078 | automotriz | hogar_servicios | 0.61 |
| ads_vertical | av079 | automotriz | tecnologia | 0.38 |
| ads_vertical | av094 | hogar_servicios | alimentos | 0.75 |
| search_intent | si032 | comercial | informacional | 0.81 |
| search_intent | si034 | comercial | informacional | 0.58 |
| sentiment | se043 | mixto | positivo | 0.54 |

### Confusiones más frecuentes (intención y sentimiento)

| label | pred | n |
|---|---|---|
| comercial | informacional | 2 |
| mixto | positivo | 1 |
