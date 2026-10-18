# Roadmap

- [x] Repo created (AGPL-3.0)
- [x] Engine research, first two passes (see ENGINES.md)
- [ ] Voice blending test (mix several reference voices)
- [ ] Stem separation step (ACE-Step 1.5 idea, check license)
- [ ] Test engine on RX 9070 XT with ROCm PyTorch in Ubuntu Distrobox
- [ ] Confirm engine pick after ROCm test (Seed-VC first, RVC/DDSP-SVC second)
- [ ] Engine server: /health and /convert
- [ ] Melody input: MIDI notes + lyrics to engine (see MELODY-SOURCES.md)
- [ ] Lyrics timing from a spoken take (forced alignment)
- [ ] JUCE plugin skeleton (VST3), then CLAP
- [ ] Plugin UI mockup in cat-ui style
- [ ] Robot-voice stage: hard retune to MIDI, dry/wet vs clean AI voice
- [ ] Dirt section: saturation, tube, distortion, bitcrush, artifacts knob
- [ ] Melody leads / voice leads toggle (ACE-Step Vocal2BGM test)
- [ ] Lyric notepad (standalone first), local LLM helper
- [ ] Flatpak packaging
- [ ] Live mode: research only, no promise
