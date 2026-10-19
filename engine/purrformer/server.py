"""AI Purrformer engine: local server. AGPL-3.0."""
from fastapi import FastAPI, HTTPException, Request, Response

from . import backends
from .analysis import analyze
from .job import SingJob
from .midi import parse_midi
from .models import downloader, setup_env
from .render import render_job
from .voicebank import RIGHTS, VoiceBank, VoiceError, default_root
from .wav import to_wav_bytes

setup_env()
app = FastAPI(title="AI Purrformer engine")
bank = VoiceBank(default_root())
try:  # voices shipped with the plugin install themselves on first start
    from .voicepack import install_bundled
    install_bundled(bank)
except Exception as exc:  # never stop the engine over voices
    print(f"bundled voices skipped: {exc}")


@app.get("/health")
def health():
    return {
        "status": "ok",
        "backends": [{"name": b.name, "available": b.available()} for b in backends.all_backends()],
    }


@app.get("/models")
def models_status():
    return downloader.status()


@app.post("/models/download")
def models_download():
    return {"started": downloader.start()}


@app.post("/sing")
def sing(job: SingJob):
    backend = backends.get(job.backend)
    if backend is None:
        raise HTTPException(status_code=400, detail=f"unknown backend '{job.backend}'")
    if not backend.available():
        raise HTTPException(status_code=503, detail=f"backend '{job.backend}' is not available")
    try:
        audio = render_job(job, backend, bank)
    except VoiceError as exc:
        raise HTTPException(status_code=400, detail=str(exc))
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


@app.post("/analyze")
async def analyze_audio(request: Request):
    """Body: WAV bytes (the song or sidechain audio). Returns detected key and chords."""
    from .voicepack import read_wav
    try:
        audio, sr = read_wav(await request.body())
    except Exception as exc:
        raise HTTPException(status_code=400, detail=f"not a readable WAV: {exc}")
    return analyze(audio, sr)


@app.get("/voices")
def voices_list():
    return {"rights_options": list(RIGHTS), "voices": [v.public() for v in bank.all_voices()]}


@app.post("/voices")
async def voices_import(request: Request, name: str, rights: str, rights_confirmed: bool = False,
                        attribution: str = "", notes: str = ""):
    """Body: a PCM .wav reference clip (1 to 60 s). Rights must be confirmed."""
    data = await request.body()
    try:
        return bank.import_voice(name, data, rights, rights_confirmed, attribution, notes).public()
    except VoiceError as exc:
        raise HTTPException(status_code=400, detail=str(exc))


@app.post("/voices/{voice_id}/clips")
async def voices_add_clip(voice_id: str, request: Request):
    try:
        return bank.add_clip(voice_id, await request.body()).public()
    except VoiceError as exc:
        raise HTTPException(status_code=400, detail=str(exc))


@app.delete("/voices/{voice_id}")
def voices_delete(voice_id: str):
    try:
        bank.delete(voice_id)
    except VoiceError as exc:
        raise HTTPException(status_code=400, detail=str(exc))
    return {"deleted": voice_id}
