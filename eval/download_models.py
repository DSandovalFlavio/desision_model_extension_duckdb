"""Descarga los modelos candidatos de la fase 0 a ./models (GGUF + ONNX)."""
import sys
from huggingface_hub import hf_hub_download, snapshot_download

GGUF = {
    "onejev1.6-0.8b": ("mradermacher/OneJev1.6-0.8B-GGUF", "OneJev1.6-0.8B.Q8_0.gguf"),
    "qwen3.5-0.8b":   ("unsloth/Qwen3.5-0.8B-GGUF", "Qwen3.5-0.8B-Q8_0.gguf"),
    "onejev1.6-4b":   ("mradermacher/OneJev1.6-4B-GGUF", "OneJev1.6-4B.Q4_K_M.gguf"),
    "qwen3.5-4b":     ("unsloth/Qwen3.5-4B-GGUF", "Qwen3.5-4B-Q4_K_M.gguf"),
    "onejev1.6-9b":   ("mradermacher/OneJev1.6-9B-GGUF", "OneJev1.6-9B.Q4_K_M.gguf"),
    "qwen3.5-9b":     ("unsloth/Qwen3.5-9B-GGUF", "Qwen3.5-9B-Q4_K_M.gguf"),
}
ONNX = {"vela3-fast-ml": "altertable/vela3-fast-multilingual-onnx-int8"}

wanted = sys.argv[1:] or list(GGUF) + list(ONNX)
for name in wanted:
    if name in GGUF:
        repo, f = GGUF[name]
        print(name, "->", hf_hub_download(repo, f, local_dir=f"models/{name.replace('onejev1.6-', 'onejev-')}"), flush=True)
    elif name in ONNX:
        print(name, "->", snapshot_download(ONNX[name], local_dir=f"models/{name.replace('onejev1.6-', 'onejev-')}"), flush=True)
