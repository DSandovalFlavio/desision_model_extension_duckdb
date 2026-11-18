"""Fase 0: evaluación de modelos de decisión sobre el set es_marketing_v1.

Backends:
  llama  -> GGUF servido por llama-server (llama.cpp oficial). Lectura "option_letter_logits":
            prompt con formato qev (OneJev) y probabilidades = softmax de los logits de las letras A, B, C...
            en la primera posición de la respuesta (sin generar texto). Sirve para OneJev y Qwen3.5 a secas.
  vela   -> vela3-fast-multilingual (encoder ONNX int8 + cabeza numpy), runtime oficial vela_onnx.py.

Uso:
  python eval/run_eval.py --model onejev1.6-0.8b --lang es
  python eval/run_eval.py --model vela3-fast-ml --lang en --limit 20
Resultados: eval/results.duckdb (tabla predictions). Informe: python eval/report.py
"""
from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

import duckdb

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "eval" / "data"
DB = ROOT / "eval" / "results.duckdb"

# id -> (backend, ruta)
MODELS = {
    "onejev1.6-0.8b": ("llama", "models/onejev-0.8b/OneJev1.6-0.8B.Q8_0.gguf"),
    "qwen3.5-0.8b":   ("llama", "models/qwen3.5-0.8b/Qwen3.5-0.8B-Q8_0.gguf"),
    "onejev1.6-4b":   ("llama", "models/onejev-4b/OneJev1.6-4B.Q4_K_M.gguf"),
    "qwen3.5-4b":     ("llama", "models/qwen3.5-4b/Qwen3.5-4B-Q4_K_M.gguf"),
    "onejev1.6-9b":   ("llama", "models/onejev-9b/OneJev1.6-9B.Q4_K_M.gguf"),
    "qwen3.5-9b":     ("llama", "models/qwen3.5-9b/Qwen3.5-9B-Q4_K_M.gguf"),
    "vela3-fast-ml":  ("vela",  "models/vela3-fast-ml"),
}

# ---------------------------------------------------------------------------------------------
# Prompt (réplica de qev/prompt.py, PROMPT_VERSION "qev-labels-v2", OneJev Apache-2.0)
# ---------------------------------------------------------------------------------------------
LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
SYSTEM_PROMPT = ("Apply the question to the state. Choose exactly one of the listed options. "
                 "Respond with only its uppercase letter, with no explanation or reasoning.")
NOUL_TRUE = "the statement is true / the answer is yes"
NOUL_FALSE = "the statement is false / the answer is no"


LAYOUT = "state_first"  # se cambia con --layout


def render_user(state: str, instructions: str, names: list[str], descs: list[str], kind: str) -> str:
    """state_first = formato oficial qev-labels-v2 (texto, luego pregunta).
    question_first = pregunta y opciones primero (prefijo idéntico en todas las filas -> se calcula una vez),
    luego el texto y la línea de respuesta. Es la optimización de rendimiento que se evalúa aquí."""
    lines = []
    for letter, n, d in zip(LETTERS, names, descs):
        lines.append(f"{letter}. {n}: {d}" if d else f"{letter}. {n}")
    letters = ", ".join(LETTERS[: len(names)])
    question = f"Question: {instructions}\n\nOptions:\n" + "\n".join(lines)
    answer = f"Answer with one letter: {letters}."
    state_block = f"<state>\n{state}\n</state>"
    if LAYOUT == "question_first":
        return f"{question}\n\n{state_block}\n\n{answer}"
    return f"{state_block}\n\n{question}\n\n{answer}"


# ---------------------------------------------------------------------------------------------
# Tareas
# ---------------------------------------------------------------------------------------------
def load_tasks():
    spec = json.loads((DATA / "tasks.json").read_text(encoding="utf-8"))
    rows = [json.loads(l) for l in (DATA / "es_marketing_v1.jsonl").read_text(encoding="utf-8").splitlines() if l.strip()]
    return spec, rows


def question_for(task: dict, lang: str):
    """-> (kind, instructions, labels, descriptions)"""
    instr = task[f"instructions_{lang}"]
    if task["type"] == "yes_no":
        return "noul", instr, ["yes", "no"], [NOUL_TRUE, NOUL_FALSE]
    labels = list(task["options"])
    return "choice", instr, labels, [task["options"][k][lang] for k in labels]


# ---------------------------------------------------------------------------------------------
# Backend llama-server
# ---------------------------------------------------------------------------------------------
def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class LlamaBackend:
    def __init__(self, gguf: str, server: str, threads: int, ngl: int, ctx: int = 4096):
        exe = shutil.which(server) or server
        self.port = _free_port()
        self.url = f"http://127.0.0.1:{self.port}"
        cmd = [exe, "-m", gguf, "--host", "127.0.0.1", "--port", str(self.port), "-c", str(ctx), "-np", "1",
               "-t", str(threads), "-ngl", str(ngl), "--no-webui"]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
                                     encoding="utf-8", errors="replace")
        self.tail: list[str] = []
        threading.Thread(target=self._drain, daemon=True).start()
        self._wait()
        self.slot_ids = self._letter_ids()
        self.build = self._get("/props").get("build_info", "")

    def _drain(self):
        for line in self.proc.stderr:
            self.tail = (self.tail + [line.rstrip()])[-40:]

    def _wait(self, timeout=600):
        t0 = time.time()
        while time.time() - t0 < timeout:
            if self.proc.poll() is not None:
                raise RuntimeError("llama-server terminó:\n" + "\n".join(self.tail))
            try:
                with urllib.request.urlopen(self.url + "/health", timeout=2) as r:
                    if r.status == 200:
                        return
            except Exception:
                pass
            time.sleep(0.5)
        raise TimeoutError("llama-server no arrancó")

    def _post(self, path, body):
        req = urllib.request.Request(self.url + path, data=json.dumps(body).encode(),
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=900) as r:
                return json.loads(r.read())
        except urllib.error.HTTPError as e:
            raise RuntimeError(f"{path} -> {e.code}: {e.read()[:400]!r}") from None

    def _get(self, path):
        with urllib.request.urlopen(self.url + path, timeout=30) as r:
            return json.loads(r.read())

    def _letter_ids(self) -> list[int]:
        ids = []
        for l in LETTERS:
            toks = self._post("/tokenize", {"content": l, "add_special": False})["tokens"]
            if len(toks) != 1:
                raise ValueError(f"la letra {l} no es un solo token")
            ids.append(toks[0])
        return ids

    def render(self, user: str) -> str:
        out = self._post("/apply-template", {
            "messages": [{"role": "system", "content": SYSTEM_PROMPT}, {"role": "user", "content": user}],
            "chat_template_kwargs": {"enable_thinking": False}})
        prompt = out["prompt"]
        if not prompt.rstrip().endswith("</think>"):
            raise ValueError("la plantilla no desactivó el razonamiento: " + prompt[-80:])
        return prompt

    def decide(self, state, kind, instructions, labels, descs):
        prompt = self.render(render_user(state, instructions, labels, descs, kind))
        n = len(labels)
        slots = self.slot_ids[:n]
        out = self._post("/completion", {"prompt": prompt, "n_predict": 1, "n_probs": max(64, 2 * n),
                                         "temperature": 0.0, "cache_prompt": False})
        top = {t["id"]: t["logprob"] for t in out["completion_probabilities"][0]["top_logprobs"]}
        mass = sum(math.exp(top[s]) for s in slots if s in top)
        if all(s in top for s in slots):
            logits = [top[s] for s in slots]
        else:  # igual que qev: forzar las letras con logit_bias y leer sus probabilidades relativas
            o2 = self._post("/completion", {"prompt": prompt, "n_predict": 1, "n_probs": n, "post_sampling_probs": True,
                                            "samplers": ["temperature"], "temperature": 1.0,
                                            "logit_bias": [[s, 100.0] for s in slots], "cache_prompt": False})
            p = {t["id"]: t["prob"] for t in o2["completion_probabilities"][0]["top_probs"]}
            logits = [math.log(max(p.get(s, 0.0), 1e-30)) for s in slots]
        m = max(logits)
        w = [math.exp(x - m) for x in logits]
        z = sum(w)
        probs = [x / z for x in w]
        ntok = out.get("tokens_evaluated") or out.get("timings", {}).get("prompt_n")
        return dict(zip(labels, probs)), ntok, mass

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()


# ---------------------------------------------------------------------------------------------
# Backend vela3-fast-ml (ONNX)
# ---------------------------------------------------------------------------------------------
class VelaBackend:
    def __init__(self, model_dir: str, threads: int):
        sys.path.insert(0, str(ROOT / model_dir))
        from vela_onnx import DecisionModel  # noqa: E402  (runtime oficial del repo HF, revisado)
        self.m = DecisionModel(str(ROOT / model_dir / "model"), threads=threads)
        self.build = "onnxruntime"

    def decide(self, state, kind, instructions, labels, descs):
        if kind == "noul":
            q = {"type": "noul", "instructions": instructions}
        else:
            q = {"type": "choice", "instructions": instructions, "criteria": dict(zip(labels, descs))}
        r = self.m.decide(state, {"q": q})
        a = r["answers"]["q"]
        probs = {"yes": a["noul"], "no": 1 - a["noul"]} if kind == "noul" else a["probabilities"]
        return probs, r["usage"]["input_tokens"], None

    def close(self):
        pass


# ---------------------------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, choices=list(MODELS))
    ap.add_argument("--lang", default="es", choices=["es", "en"], help="idioma de la pregunta y las opciones")
    ap.add_argument("--tasks", default="all")
    ap.add_argument("--limit", type=int, default=0, help="máx. ejemplos por tarea (0 = todos)")
    ap.add_argument("--threads", type=int, default=os.cpu_count())
    ap.add_argument("--ngl", type=int, default=0, help="capas en GPU para llama-server (-1 = todas)")
    ap.add_argument("--server", default=str(ROOT / "tools/llama-b12521-cuda12/llama-server.exe"))
    ap.add_argument("--note", default="")
    ap.add_argument("--layout", default="state_first", choices=["state_first", "question_first"])
    args = ap.parse_args()
    global LAYOUT
    LAYOUT = args.layout

    spec, rows = load_tasks()
    tasks = list(spec["tasks"]) if args.tasks == "all" else args.tasks.split(",")
    if args.limit:
        seen: dict[str, int] = {}
        keep = []
        for r in rows:
            seen[r["task"]] = seen.get(r["task"], 0) + 1
            if seen[r["task"]] <= args.limit:
                keep.append(r)
        rows = keep
    rows = [r for r in rows if r["task"] in tasks]

    backend_kind, path = MODELS[args.model]
    t_load = time.perf_counter()
    be = (LlamaBackend(str(ROOT / path), args.server, args.threads, args.ngl) if backend_kind == "llama"
          else VelaBackend(path, args.threads))
    t_load = time.perf_counter() - t_load
    run_id = time.strftime("%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:4]
    print(f"[{run_id}] {args.model} lang={args.lang} layout={args.layout} filas={len(rows)} carga={t_load:.1f}s build={be.build}", flush=True)

    con = duckdb.connect(str(DB))
    con.execute("""CREATE TABLE IF NOT EXISTS predictions (
        run_id VARCHAR, model VARCHAR, prompt_lang VARCHAR, layout VARCHAR, dataset VARCHAR, task VARCHAR, id VARCHAR,
        label VARCHAR, pred VARCHAR, confidence DOUBLE, p_label DOUBLE, probs JSON,
        latency_ms DOUBLE, n_tokens INTEGER, letter_mass DOUBLE, threads INTEGER, ngl INTEGER,
        host VARCHAR, build VARCHAR, note VARCHAR, ts TIMESTAMP DEFAULT current_timestamp)""")

    con.execute("ALTER TABLE predictions ADD COLUMN IF NOT EXISTS layout VARCHAR DEFAULT 'state_first'")
    host = os.environ.get("COMPUTERNAME", "")
    t_all = time.perf_counter()
    try:
        for i, r in enumerate(rows, 1):
            kind, instr, labels, descs = question_for(spec["tasks"][r["task"]], args.lang)
            t0 = time.perf_counter()
            probs, ntok, mass = be.decide(r["text"], kind, instr, labels, descs)
            ms = (time.perf_counter() - t0) * 1000
            pred = max(probs, key=probs.get)
            con.execute("INSERT INTO predictions (run_id, model, prompt_lang, layout, dataset, task, id, label, pred, confidence, "
                        "p_label, probs, latency_ms, n_tokens, letter_mass, threads, ngl, host, build, note) "
                        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                        [run_id, args.model, args.lang, args.layout, spec["version"], r["task"], r["id"], r["label"], pred,
                         probs[pred], probs.get(r["label"], 0.0), json.dumps(probs), ms, ntok, mass,
                         args.threads, args.ngl, host, be.build, args.note])
            if i % 20 == 0 or i == len(rows):
                el = time.perf_counter() - t_all
                acc = con.execute("SELECT avg((pred=label)::INT) FROM predictions WHERE run_id=?", [run_id]).fetchone()[0]
                print(f"  {i}/{len(rows)}  {i/el:.2f} filas/s  acc={acc:.3f}", flush=True)
    finally:
        be.close()
        con.close()


if __name__ == "__main__":
    main()
