# Inspiration and borrowable ideas

Checked 2026-10-17 from each GitHub page.

## innermost47/ai-dj (Obsidian Neural)
- AGPL-3.0 VST3 + standalone plugin. Linux builds listed.
- Same shape as our plan: plugin plus optional GPU server, URL entered in plugin settings.
- Their GPU server is NVIDIA CUDA only (and Apple Silicon). Ours must run on ROCm.
- Models are separate downloads under their own terms (Stability AI Community License, Gemma Terms). Not clean FOSS. Do not copy that model choice.
- Borrow: server URL in Settings, works-without-server fallback, standalone app next to the plugin.

## yeeking/ai-improviser-plugin
- MIT. JUCE built with CMake, JUCE cloned into the project folder.
- Builds a standalone app and a VST3 (no CLAP mentioned).
- Linux: build it yourself.
- Borrow: the CMake layout as a starting template for `plugin/`.

## SoulX-Singer
- Apache-2.0. Singing synthesis from melody/MIDI plus zero-shot voice conversion.
- Described as timbre cloning and style transfer. Voice mixing is not mentioned on its page.
- Candidate engine, see ENGINES.md.

## ACE-Step 1.5
- MIT code. Lyrics, covers, stems, LoRA training, lyric timestamps (LRC).
- Only one in the list that documents ROCm on Linux.
- Borrow: stem separation (clean vocal out of a take), LRC timing, and a "song sketch" from lyrics for the lyric tool. Check the weights license first.

## YuE
- Apache-2.0 including weights. Lyrics to full song with vocals, voice reference via in-context learning.
- Needs 24 GB or less with 2 sessions; FlashAttention 2 is mandatory. Heavy and CUDA-leaning. Reference only.

## Idea: mixing voices into a new voice (to test, not confirmed)
- Zero-shot engines take a reference clip. Try blending: feed a mix of several reference voices, or interpolate between speaker embeddings if the engine exposes them.
- Plugin UI: 2 to 4 "voice slots" with a blend slider each, like an XY mixer.
- Needs a test per engine before we promise it. Also check the rights on every reference voice.
