# Engine

Local Python server. Runs in an Ubuntu container with ROCm PyTorch for the real AI backends.
The `tone` backend needs no GPU and plays the notes as sine tones, for testing the plugin.

## Run

    cd engine
    pip install -r requirements.txt
    uvicorn purrformer.server:app --host 127.0.0.1 --port 8765

## Test

    pip install -r requirements-dev.txt
    python -m pytest

## API

- `GET /health`: status and the list of backends (name, available)
- `POST /sing`: body is a job (see `purrformer/job.py`), returns a WAV
- `POST /midi/parse`: body is raw `.mid` bytes, returns tempo and notes in job format
- `GET/POST/DELETE /voices`: the voice bank, see `docs/VOICES.md`

## Job format (ours)

Times are in beats. Fields: `mode` (melody_leads or voice_leads), `backend`, `sample_rate`,
`tempo_bpm`, `notes` (start_beats, length_beats, pitch, velocity, lyric), `lyrics`,
`voices` (id + weight for blending), `fx` (robot, dirt).
Adapters turn this into what each AI backend needs.

## Adding a backend

Implement `Backend` in `purrformer/backends/base.py` and call `register()`.

## FX prototype

`purrformer/fx.py` holds the robot retune (pitch tracker + delay-line shifter), the supersaw voice stack (detuned copies, stereo spread, widener) and the dirt stage. With supersaw on, the output is stereo.
`/sing` applies `job.fx` after the backend renders. Make listening demos with:

    pip install scipy
    python tools/make_demos.py OUT_DIR

The demos use a synthetic voice. Judge the effect by ear, then tune. Known limits: formants move with the pitch shift, grain artifacts are not tuned.
