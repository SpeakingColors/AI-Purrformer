# Where the melody comes from

Design rule: the user's own voice does NOT provide pitch. The user speaks or sings monotone, so pitch correction of the take is not the answer. The notes come from somewhere else.

Checked 2026-10-17 from GitHub pages. "Not stated" means the page does not say.

## Melody sources

1. **MIDI from the DAW** (main route). The user plays or draws the melody in a MIDI clip. The plugin sends the notes to the engine.
2. **A guide vocal.** Any sound that carries the melody (a synth lead, a rendered vocal, the original singer of a song). The engine keeps the melody and swaps the voice.
3. **A song's own vocal.** Separate the vocal from a song, extract pitch and notes, then re-sing it with another voice. Only with material the user has rights to.

## What the user's voice is used for

- Lyrics timing: when each word or syllable is said (forced alignment).
- Optionally timbre, as the reference for the new voice. Mixed with other voices if we get blending working.

## Engine fit (SoulX-Singer, from its page)

- Singing synthesis can be conditioned on a melody (F0 contour) or on a score (MIDI notes).
- Singing voice conversion takes waveform and F0 inputs and converts into the prompt timbre.
- Zero-shot for unseen singers.
- Its preprocessing does: vocal separation and dereverb, F0 extraction, voice activity detection, lyrics transcription, note transcription.
- The page warns automatic alignment may not be perfect and points to a MIDI Editor for manual fixing.
- Languages: dataset described as Mandarin, English and Cantonese. English is covered, other languages not stated.
- The per-note metadata JSON schema is NOT shown on the pages read. Must read the code or example files before building the plugin side.
- Preprocessing tools named: Music-Source-Separation-Training, mel-band-roformer-karaoke, dereverb_mel_band_roformer, RMVPE, Paraformer, Parakeet-tdt-0.6b-v2, ROSVOT. Their licenses are not stated on that page. Check each before bundling.

## Other tools

- **OpenUtau**: MIT, runs on Windows, macOS and Linux, MIDI-style editor, plugin system. Its page does not mention DiffSinger voicebanks or MIDI/lyric import, so do not assume them. Useful as a UI reference, and possibly as an editor.
- **Montreal Forced Aligner**: MIT, aligns text to speech with Kaldi. Linux not confirmed on its page (conda install is given). Languages not stated there. Candidate for lyrics timing from a spoken take.
- **DiffSinger**: Apache-2.0, singing synthesis, melody frontend topics. Linux and AMD not stated.

## Draft modes for the plugin

- **Score to voice:** lyrics typed in the plugin + MIDI notes from the DAW clip, engine sings it. (Needs the note+lyric format worked out.)
- **Guide to voice:** a guide vocal audio clip in, new voice out, same melody.
- **Speak it:** user speaks the lyrics, aligner finds the timing, notes from MIDI are laid over that timing.

## Open questions

- Exact SoulX-Singer input schema (read `example/` and `preprocess/` code).
- Does SoulX-Singer SVS run on ROCm PyTorch on the RX 9070 XT.
- Syllable splitting for English lyrics across notes: which tool, which license.
- How the plugin gets MIDI notes: as an instrument receiving a clip, or by reading the host clip (host dependent, test in Bitwig and Studio One).
