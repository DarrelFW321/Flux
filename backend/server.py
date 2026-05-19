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
# Each kernel in `KERNELS` has a Flux source, a hand-written C source, and a
# Python+NumPy script that all compute the same number. We compile the Flux
# and C versions to native binaries (cached on disk between calls), then time
# an end-to-end subprocess run of all three implementations with
# `time.perf_counter`. Returns per-kernel timings plus the printed result so
# the UI can show the implementations agree numerically.

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
    proc  = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or "non-zero exit")
    return elapsed_ms, proc.stdout.strip()


def _ensure_flux_binary(stem: str, out_dir: str) -> str:
    """Compile benchmarks/<stem>.fl to `out_dir/<stem>_flux(.exe)`."""
    src = os.path.join(BENCH_DIR, f"{stem}.fl")
    out = os.path.join(out_dir, f"{stem}_flux" + _exe_suffix())
    if os.path.exists(out):
        return out
    proc = subprocess.run(
        [FLUX_BIN, "--compile", src, "-o", out],
        capture_output=True, text=True, timeout=120,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            "flux compile failed: " + (proc.stderr.strip() or proc.stdout.strip())
        )
    return out


def _ensure_c_binary(stem: str, out_dir: str) -> str:
    """Compile benchmarks/<stem>.c to `out_dir/<stem>_c(.exe)` with -O2."""
    gcc = _gcc_executable()
    if not gcc:
        raise RuntimeError("gcc not found on PATH; cannot compile C benchmark")
    src = os.path.join(BENCH_DIR, f"{stem}.c")
    out = os.path.join(out_dir, f"{stem}_c" + _exe_suffix())
    if os.path.exists(out):
        return out
    proc = subprocess.run(
        [gcc, "-O2", "-o", out, src],
        capture_output=True, text=True, timeout=120,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            "gcc compile failed: " + (proc.stderr.strip() or proc.stdout.strip())
        )
    return out


# Cache compiled binaries inside the OS tempdir so repeat calls skip recompilation.
_BENCH_CACHE_DIR = os.path.join(tempfile.gettempdir(), "flux_bench")
os.makedirs(_BENCH_CACHE_DIR, exist_ok=True)


KERNELS = [
    {
        "stem":        "dot",
        "name":        "dot",
        "description": "8-element dot product × 1,000,000 iterations — per-call overhead",
    },
    {
        "stem":        "saxpy",
        "name":        "saxpy",
        "description": "sum(2·x + y) on 64-element vectors × 200,000 iterations — broadcast + add + reduce",
    },
    {
        "stem":        "relu",
        "name":        "relu",
        "description": "sum(relu(x)) on 64-element vectors × 200,000 iterations — element-wise ML activation",
    },
    {
        "stem":        "bigdot",
        "name":        "bigdot",
        "description": "512-element dot product × 50,000 iterations — same total FLOPs as `dot`, NumPy gets a fair shot",
    },
]


def _read_benchmark_sources(stem: str) -> dict[str, str]:
    """Load Flux, C, and NumPy sources for a benchmark kernel."""
    out: dict[str, str] = {}
    for key, ext in (("flux", "fl"), ("c", "c"), ("numpy", "py")):
        path = os.path.join(BENCH_DIR, f"{stem}.{ext}")
        try:
            with open(path, encoding="utf-8") as f:
                out[key] = f.read()
        except OSError:
            out[key] = ""
    return out


def _run_one_kernel(stem: str, name: str, description: str) -> dict:
    results: list[dict] = []

    def run_one(impl: str, builder) -> None:
        try:
            cmd        = builder()
            time_ms, out = _time_subprocess(cmd)
            results.append({"name": impl, "time_ms": time_ms, "output": out, "error": None})
        except Exception as e:
            results.append({"name": impl, "time_ms": None, "output": None, "error": str(e)})

    run_one("Flux",        lambda: [_ensure_flux_binary(stem, _BENCH_CACHE_DIR)])
    run_one("C (gcc -O2)", lambda: [_ensure_c_binary  (stem, _BENCH_CACHE_DIR)])
    run_one("NumPy",       lambda: [sys.executable, os.path.join(BENCH_DIR, f"{stem}.py")])

    return {
        "kernel":      name,
        "description": description,
        "sources":     _read_benchmark_sources(stem),
        "results":     results,
        "error":       None,
    }


@app.post("/benchmark")
def run_benchmark():
    """Run every registered kernel across Flux, C, and NumPy.

    Returns
    -------
        {
          "kernels": [
            {
              "kernel":      "<short name>",
              "description": "<one-line description>",
              "results":     [{name, time_ms, output, error}, ...],
              "error":       null,
            },
            ...
          ],
          "error": null,
        }
    """
    kernels = [_run_one_kernel(k["stem"], k["name"], k["description"]) for k in KERNELS]
    return {"kernels": kernels, "error": None}
