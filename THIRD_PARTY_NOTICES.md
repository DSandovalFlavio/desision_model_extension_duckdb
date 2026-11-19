# Avisos de terceros

## altertable-ai/duckdb-decide_onnx (MIT)

`src/decide_grammar.cpp` es un port adaptado de `src/decide_grammar.cpp` de https://github.com/altertable-ai/duckdb-decide_onnx; la interfaz SQL (`decision_choose`, `decision_yes_no`, `DECIDE`) sigue la de ese proyecto.

```
Copyright 2018-2025 Stichting DuckDB Foundation

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.```

## OmniJev/OneJev (Apache-2.0)

El formato de prompt `qev-labels-v2` (src/decide_prompt.cpp) y la lectura por letras replican `qev/prompt.py` y `qev/llamacpp.py` de https://github.com/OmniJev/OneJev (Apache-2.0).

## llama.cpp (MIT) y DuckDB (MIT)

Submódulos `third_party/llama.cpp` y `duckdb`, con sus propias licencias.

## NVIDIA CUDA runtime (zips `cuda12` de los Releases)

Los zips `cuda12` incluyen `cudart64_12.dll`, `cublas64_12.dll` y `cublasLt64_12.dll`, distribuidos bajo el [NVIDIA CUDA Toolkit EULA](https://docs.nvidia.com/cuda/eula/) (componentes redistribuibles, Attachment A). No forman parte del código de este repositorio.
