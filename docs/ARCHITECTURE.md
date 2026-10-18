# Architecture (draft)

```
DAW (Bitwig / Studio One)
  └─ AI Purrformer plugin (JUCE, VST3/CLAP)
        │  localhost HTTP, audio clip in, audio clip out
        ▼
  Purrformer engine (Python, in Ubuntu Distrobox, ROCm PyTorch)
        ├─ 1. melody: MIDI notes / guide vocal (NOT the user's pitch)
        ├─ 2. lyrics + timing
        ├─ 3. sing in the chosen voice (SoulX-Singer SVS/SVC candidate)
        ├─ 4. FX: hard retune to MIDI (robot) + dirt, dry/wet vs clean voice
        └─ 5. return audio
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
- Melody source rules: see docs/MELODY-SOURCES.md
