import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

app = FastAPI(title="Flux Compiler API")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["POST", "GET"],
    allow_headers=["*"],
)

# Path to the flux binary — same directory as this script when running in Docker.
FLUX_BIN = os.path.join(os.path.dirname(__file__), "..", "build", "flux")


class CompileRequest(BaseModel):
    source: str


@app.get("/")
def health():
    return {"status": "ok"}


@app.post("/run")
def run_source(req: CompileRequest):
    """
    Compile source to a native Linux binary and execute it.
    Returns { output, error }.
    """
    with tempfile.NamedTemporaryFile(
        suffix=".fl", delete=False, mode="w", encoding="utf-8"
    ) as f:
        f.write(req.source)
        tmp_path = f.name

    out_path = tmp_path + ".out"

    try:
        # Compile to native binary
        compile_result = subprocess.run(
            [FLUX_BIN, "--compile", tmp_path, "-o", out_path],
            capture_output=True, text=True, timeout=30,
        )
        if compile_result.returncode != 0:
            return {"output": None, "error": compile_result.stderr.strip() or compile_result.stdout.strip()}

        # Execute with a strict timeout
        run_result = subprocess.run(
            [out_path],
            capture_output=True, text=True, timeout=5,
        )
        return {
            "output": run_result.stdout,
            "error":  run_result.stderr.strip() or None,
        }
    except subprocess.TimeoutExpired:
        return {"output": None, "error": "Program timed out (5 s limit)"}
    except Exception as e:
        return {"output": None, "error": str(e)}
    finally:
        os.unlink(tmp_path)
        if os.path.exists(out_path):
            os.unlink(out_path)


EMPTY_STAGES = {
    "tokens": None, "ast": None,
    "mir_raw": None, "mir_optimized": None, "passes": [],
    "ir": None,
}

# Benchmarks live at the repo root, one directory above /app/backend on the
# deployed image and one directory above this file in local dev.
BENCH_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "benchmarks"))


@app.post("/compile")
def compile_source(req: CompileRequest):
    """
    Run the flux compiler pipeline on the submitted source code.

    Returns the full --dump-stages payload:
      { tokens, ast, mir_raw, mir_optimized, passes, ir, error }

    tokens and ast are parsed objects; mir_raw/mir_optimized/ir are strings;
    passes is a list of { name, changes }.
    """
    with tempfile.NamedTemporaryFile(
        suffix=".fl", delete=False, mode="w", encoding="utf-8"
    ) as f:
        f.write(req.source)
        tmp_path = f.name

    try:
        result = subprocess.run(
            [FLUX_BIN, "--dump-stages", tmp_path],
            capture_output=True,
            text=True,
            timeout=30,
        )
        # flux --dump-stages always outputs valid JSON (error field populated on failure)
        if result.stdout.strip():
            return json.loads(result.stdout)
        return {**EMPTY_STAGES,
                "error": result.stderr.strip() or "flux produced no output"}
    except subprocess.TimeoutExpired:
        return {**EMPTY_STAGES, "error": "Compilation timed out"}
    except Exception as e:
        return {**EMPTY_STAGES, "error": str(e)}
    finally:
        os.unlink(tmp_path)


# ── Benchmark endpoint ───────────────────────────────────────────────────────
#
# Compiles `benchmarks/dot.fl` and `benchmarks/dot.c` to native binaries (if
# they're not already cached on disk), then times an end-to-end run of all
# three implementations (Flux, C, NumPy) with `time.perf_counter`. Returns
# per-implementation wall-clock millis plus the program output so the
# frontend can show they computed identical results.

def _exe_suffix() -> str:
    return ".exe" if sys.platform.startswith("win") else ""


def _gcc_executable() -> str | None:
    # Honour explicit override first (handy on Windows w/ msys2).
    override = os.environ.get("CC")
    if override and shutil.which(override):
        return override
    for candidate in ("gcc", "cc"):
        path = shutil.which(candidate)
        if path:
            return path
    return None


def _time_subprocess(cmd: list[str]) -> tuple[float, str]:
    """Run `cmd`, return (wall_ms, stdout). Raises on failure."""
    start = time.perf_counter()
    proc  = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or "non-zero exit")
    return elapsed_ms, proc.stdout.strip()


def _ensure_flux_binary(out_dir: str) -> str:
    """Compile benchmarks/dot.fl to `out_dir/dot_flux(.exe)` via the flux binary."""
    src = os.path.join(BENCH_DIR, "dot.fl")
    out = os.path.join(out_dir, "dot_flux" + _exe_suffix())
    if os.path.exists(out):
        return out
    proc = subprocess.run(
        [FLUX_BIN, "--compile", src, "-o", out],
        capture_output=True, text=True, timeout=60,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            "flux compile failed: " + (proc.stderr.strip() or proc.stdout.strip())
        )
    return out


def _ensure_c_binary(out_dir: str) -> str:
    """Compile benchmarks/dot.c to `out_dir/dot_c(.exe)` with -O2."""
    gcc = _gcc_executable()
    if not gcc:
        raise RuntimeError("gcc not found on PATH; cannot compile C benchmark")
    src = os.path.join(BENCH_DIR, "dot.c")
    out = os.path.join(out_dir, "dot_c" + _exe_suffix())
    if os.path.exists(out):
        return out
    proc = subprocess.run(
        [gcc, "-O2", "-o", out, src],
        capture_output=True, text=True, timeout=60,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            "gcc compile failed: " + (proc.stderr.strip() or proc.stdout.strip())
        )
    return out


# Cache compiled binaries inside the OS tempdir so repeat calls skip recompilation.
_BENCH_CACHE_DIR = os.path.join(tempfile.gettempdir(), "flux_bench")
os.makedirs(_BENCH_CACHE_DIR, exist_ok=True)


@app.post("/benchmark")
def run_benchmark():
    """
    Run the dot-product microbenchmark across three implementations.

    Returns
    -------
        {
          "kernel": "dot",
          "description": "...",
          "results": [
            { "name": "Flux", "time_ms": float, "output": str, "error": str|None },
            { "name": "C (gcc -O2)", ... },
            { "name": "NumPy", ... }
          ],
          "error": null
        }
    """
    results: list[dict] = []

    def run_one(name: str, builder) -> None:
        try:
            cmd        = builder()
            time_ms, out = _time_subprocess(cmd)
            results.append({"name": name, "time_ms": time_ms, "output": out, "error": None})
        except Exception as e:
            results.append({"name": name, "time_ms": None, "output": None, "error": str(e)})

    run_one("Flux",         lambda: [_ensure_flux_binary(_BENCH_CACHE_DIR)])
    run_one("C (gcc -O2)",  lambda: [_ensure_c_binary  (_BENCH_CACHE_DIR)])
    run_one("NumPy",        lambda: [sys.executable, os.path.join(BENCH_DIR, "dot.py")])

    return {
        "kernel":      "dot",
        "description": "1M iterations of an 8-element dot product (per-call overhead test)",
        "results":     results,
        "error":       None,
    }
