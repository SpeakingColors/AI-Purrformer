"""AI Purrformer engine: local server skeleton. AGPL-3.0."""
from fastapi import FastAPI, HTTPException

app = FastAPI(title="AI Purrformer engine")


@app.get("/health")
def health():
    return {"status": "ok", "engine": None}


@app.post("/convert")
def convert():
    raise HTTPException(status_code=501, detail="Not implemented yet")
