import json
import os
import subprocess
import tempfile

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
