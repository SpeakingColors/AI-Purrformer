# Architecture (draft)

```
DAW (Bitwig / Studio One)
  └─ AI Purrformer plugin (JUCE, VST3/CLAP)
        │  localhost HTTP, audio clip in, audio clip out
        ▼
  Purrformer engine (Python, in Ubuntu Distrobox, ROCm PyTorch)
        ├─ 1. pitch + timing correction
        ├─ 2. voice conversion (RVC or DDSP-SVC, TBD)
        └─ 3. return audio
```

## Why split plugin and engine

- Plugin stays small, stable and safe in the DAW. A crashed engine does not crash the DAW.
- Python AI stack needs ROCm on Ubuntu, which Bazzite does not provide natively.

## Plugin

- Records or takes a clip from the DAW timeline, shows status, drops the result back.
- Engine address and voice model chosen in Settings.
- Cat-ui look, resizable, light on CPU/GPU, stops drawing when not in front.

## Engine API (draft)

- `GET /health` returns status and device info
- `POST /convert` takes audio and options, returns audio (not implemented yet)

## Open questions

- Audio transfer: file path vs upload (same machine, so file path may be simpler)
- CLAP via JUCE: verify the extension route before committing
- Whether pitch correction lives in the plugin or the engine
