# FX, inferred accompaniment and lyric AI

Checked 2026-10-17 from GitHub pages. "Not stated" means the page does not say.

## 1. Robot voice (T-Pain / Daft Punk style) and dirt

Idea: a hard-tune stage after the AI voice, mixed with the clean AI voice by a dry/wet knob.

- The target notes are the MIDI melody we already have, so the retune can snap exactly to it. This is a design idea, to be built and tested.
- Knobs (draft): retune speed (instant = robotic, slow = natural), dry/wet vs the clean AI voice, formant shift, vibrato amount.
- Dirt section (we write our own DSP, nothing here is borrowed code yet): tube/soft-clip saturation, distortion, bitcrush, sample-rate reduction, stepped-pitch glitches, formant wobble. A "artifacts" amount knob keeps it on purpose, not accidental.
- Same dry/wet idea as the Dance MB default in the other plugins.

Reference tools:
- fat1.lv2 (x42): auto-tuner based on zita-at1, LV2, licence listed as GPL-2.0. Its page does not say whether it is GPL-2.0-only or "or later", so mixing its code with our AGPL-3.0 is NOT confirmed safe. Use as a reference or run it as a separate plugin; do not copy code until checked.
- Airwindows: MIT, "all the Airwindows open source code". The page does not list which effects (saturation, tube, etc.) or Linux builds. Check the repo contents before relying on it.

## 2. Melody leads vs voice leads (toggle)

- Melody leads (default): MIDI melody drives the singer.
- Voice leads: the sung line comes first and the accompaniment (chords, bass, rhythm) is inferred to fit it.

What the pages say:
- ACE-Step 1.5 lists vocal-to-accompaniment generation (Vocal2BGM), BPM/key/duration control, stem separation and lyric timestamps. MIT code; weights licence not stated. Best fit for the voice-leads mode.
- SoulX-Singer preprocessing lists note transcription, so a sung line can be turned back into notes.

Honest gap: a singer always needs notes from somewhere. For voice-leads mode the vocal line itself needs a source: the user's MIDI sketch, or a draft generated from lyrics + key + BPM (ACE-Step makes songs from lyrics per its page), then transcribed to notes and re-sung in the chosen voice. This chain is an idea to test, not a confirmed workflow.

## 3. Lyric helper (local LLM)

Yes, doable fully offline.
- llama.cpp: MIT. Backend table lists HIP for AMD GPUs and Vulkan for GPU. Page shows an OpenAI-compatible server. ROCm is not named on the page and Linux is not stated there, so test on the RX 9070 XT.
- Qwen3 models: Apache-2.0 open weights, sizes 0.6B to 32B (plus MoE), 100+ languages. A small one (4B or 8B) is a likely start. Check the exact model card licence when picking.
- Do not pick models with custom terms (e.g. Gemma Terms of Use, Stability Community License) as defaults. Keep the default model Apache-2.0 or MIT.

Helper features (draft):
- Autocomplete the next line, rhyme suggestions, theme prompts, rewrite in a style (dance, hardstyle, UK hardcore).
- Syllable fit: count the notes in each MIDI phrase and suggest lines with that many syllables, so lyrics land on the melody.
- Notepad with sections (verse, hook, drop) and a "send to singer" button.
- Runs as a separate `lyrics/` tool first, then inside the plugin.

## 4. Supersaw voice (added 2026-10-18)

Same idea as a supersaw oscillator, applied to the voice: 3 to 9 copies, outer copies detuned up to +-50 cents, each with a slow private drift, panned across the stereo field. The centre copy stays unshifted. A widener boosts the side signal. Controls: copies, detune, stack mix, spread, widen. Output becomes stereo. Prototype only, tuned by measurement not by ear.
Order in the chain: clean voice, robot, supersaw, dirt.
