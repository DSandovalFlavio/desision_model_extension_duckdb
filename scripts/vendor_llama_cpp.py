"""Copy the parts of llama.cpp that decide_amox compiles into third_party/llama.cpp.

llama.cpp is vendored (plain files, not a git submodule) because its full tree has paths longer than the Windows
MAX_PATH limit (ui/...), which breaks `actions/checkout` with submodules on the Windows runners of DuckDB's
extension CI. Only the build inputs are kept: the CMake files, include/, src/, vendor/ and ggml/ with the CPU,
BLAS, Metal and CUDA backends.

Update to another llama.cpp release:
    python scripts/vendor_llama_cpp.py <path-to-a-llama.cpp-checkout-at-the-new-tag> <tag>
then set DECIDE_LLAMA_BUILD in CMakeLists.txt and rebuild/test.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

KEEP_TOP = ["CMakeLists.txt", "LICENSE", "cmake", "include", "src", "ggml", "vendor"]
KEEP_GGML_BACKENDS = {"ggml-cpu", "ggml-blas", "ggml-metal", "ggml-cuda"}
DROP_VENDOR = {"cpp-httplib"}  # only used by llama.cpp's common/ library, which is not built


def main() -> None:
    src = Path(sys.argv[1]).resolve()
    tag = sys.argv[2]
    root = Path(__file__).resolve().parent.parent
    dst = root / "third_party" / "llama.cpp"
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True)
    count = 0
    for top in KEEP_TOP:
        s = src / top
        if s.is_file():
            shutil.copy2(s, dst / top)
            count += 1
            continue
        for dirpath, dirnames, filenames in os.walk(s):
            rel = Path(dirpath).relative_to(src).as_posix()
            if rel == "ggml/src":
                dirnames[:] = [d for d in dirnames if not d.startswith("ggml-") or d in KEEP_GGML_BACKENDS]
            if rel == "vendor":
                dirnames[:] = [d for d in dirnames if d not in DROP_VENDOR]
            for f in filenames:
                target = dst / rel / f
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(Path(dirpath) / f, target)
                count += 1
    commit = subprocess.run(["git", "-C", str(src), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    (dst / "VENDORED.md").write_text(
        f"# Vendored llama.cpp\n\n- Upstream: https://github.com/ggml-org/llama.cpp\n- Tag: `{tag}`\n- Commit: `{commit}`\n"
        f"- Contents: {', '.join(KEEP_TOP)}; ggml backends {', '.join(sorted(KEEP_GGML_BACKENDS))} only.\n"
        f"- Produced by `scripts/vendor_llama_cpp.py`. Do not edit these files by hand.\n",
        encoding="utf-8")
    longest = max((len("third_party/llama.cpp/" + p.relative_to(dst).as_posix()) for p in dst.rglob("*")), default=0)
    size = sum(p.stat().st_size for p in dst.rglob("*") if p.is_file())
    print(f"{count} files, {size / 1e6:.1f} MB, longest path {longest} chars -> {dst}")


if __name__ == "__main__":
    main()
