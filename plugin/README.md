# Plugin

JUCE 8 plugin (AGPL-3.0): VST3 and Standalone. IS_SYNTH, takes MIDI, outputs stereo audio.

What works now (tested by `purr_selftest` against the real engine):
- Capture the melody from the host's MIDI while the transport plays, or open a .mid file.
- Render through the engine (test voices for now), with robot, supersaw and dirt settings.
- Playback follows the host transport (position comes from the host beat, so loops and jumps stay in sync).
- State saves and loads (notes, lyrics, parameters, chosen voices).
- Four screens (Sing, Voice FX, Lyrics, Voices) that redraw live from the controls.

Not done yet: real AI voices (needs the ROCm test), lyric AI helper, CLAP, Flatpak extension, tempo-change re-sync (the UI does not yet warn you).

Build and install: see `docs/BUILD.md`.
