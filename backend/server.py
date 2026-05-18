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


@app.post("/compile")
def compile_source(req: CompileRequest):
    """
    Run the flux compiler pipeline on the submitted source code.
    Returns { tokens, ast, ir, error } as JSON.
    The tokens and ast fields are already parsed objects (not strings).
    The ir field is a string.
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
        # Unexpected: no stdout at all
        return {"tokens": None, "ast": None, "ir": None,
                "error": result.stderr.strip() or "flux produced no output"}
    except subprocess.TimeoutExpired:
        return {"tokens": None, "ast": None, "ir": None, "error": "Compilation timed out"}
    except Exception as e:
        return {"tokens": None, "ast": None, "ir": None, "error": str(e)}
    finally:
        os.unlink(tmp_path)
