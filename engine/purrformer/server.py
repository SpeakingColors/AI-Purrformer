"""AI Purrformer engine: local server. AGPL-3.0."""
from fastapi import FastAPI, HTTPException, Request, Response

from . import backends
from .job import SingJob
from .midi import parse_midi
from .wav import to_wav_bytes

app = FastAPI(title="AI Purrformer engine")


@app.get("/health")
def health():
    return {
        "status": "ok",
        "backends": [{"name": b.name, "available": b.available()} for b in backends.all_backends()],
    }


@app.post("/sing")
def sing(job: SingJob):
    backend = backends.get(job.backend)
    if backend is None:
        raise HTTPException(status_code=400, detail=f"unknown backend '{job.backend}'")
    if not backend.available():
        raise HTTPException(status_code=503, detail=f"backend '{job.backend}' is not available")
    audio = backend.sing(job)
    return Response(content=to_wav_bytes(audio, job.sample_rate), media_type="audio/wav")


@app.post("/midi/parse")
async def midi_parse(request: Request):
    """Body: raw .mid bytes. Returns tempo and notes in our job format."""
    data = await request.body()
    try:
        parsed = parse_midi(data)
    except Exception as exc:  # mido raises several error types on bad files
        raise HTTPException(status_code=400, detail=f"not a valid MIDI file: {exc}")
    return {"tempo_bpm": parsed.tempo_bpm, "notes": [n.model_dump() for n in parsed.notes]}
