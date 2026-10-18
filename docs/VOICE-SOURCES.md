# Where voices can come from

AI Purrformer ships no voices (see VOICES.md). This is what we found online, checked against the source pages on 2026-10-18.

## How the engines use voices
SoulX-Singer (Apache-2.0) does not bundle voices. It does zero-shot synthesis: you give it a reference clip as the timbre prompt. So a "voice pack" for us is a set of clean reference clips with a licence that allows the use. Its page says: respect IP, privacy and consent, no impersonating real people, no deceptive audio.

## Datasets found (reference clips, not finished voice banks)
| Dataset | Content | Licence (from its page) | Fit |
|---|---|---|---|
| VocalSet (Zenodo 1442513) | 20 pro singers (9 male, 11 female), 10.1 h, vowels, scales, arpeggios, long tones, excerpts | CC BY 4.0 | Best fit: attribution only. Language not stated, no known lyrics, so good for timbre prompts, not for English diction |
| CSD, Children's Song Dataset (Zenodo 4785016) | 1 Korean female pop singer, 50 Korean + 50 English songs | CC BY-NC-SA 4.0, research only | Non-commercial: avoid if you release music |
| GTSinger | 20 singers, 80.6 h singing, 9 languages incl. English | CC BY-NC-SA 4.0 | Non-commercial: avoid if you release music. Good for testing only |
| VCTK (Edinburgh DataShare 10283/3443) | 110 English speakers (speech, not singing) | Page did not state the licence, must read license_text.txt | Unverified, do not use until read |

## Not verified
- DiffSinger / OpenUtau community voicebanks: each bank has its own licence, our search found no list. Check bank by bank.
- Whether voice-cloning from NC datasets counts as "commercial" when you release the song: ask a lawyer, we do not guess.

## Plan
1. Own voice clips (spoken is fine for timbre), rights type `own-voice`.
2. VocalSet clips as built-in-optional pack, `cc-by`, with the attribution shown in About (the voice bank already supports this).
3. Friends or singers who give written permission (`permission`).
4. Language: English first. SoulX-Singer's training data covers Mandarin, English and Cantonese per its README; other languages are not claimed by it.

## Installing a pack (added 2026-10-18)
The cloud workspace cannot download datasets, so no real voice is bundled. You download a pack yourself (so you see its licence page), then:
- VocalSet (CC BY 4.0): download the zip from its Zenodo page, then `python -m purrformer.voicepack vocalset VocalSet.zip`. It makes one voice per singer from sustained "ah" clips, sets rights `cc-by` with the attribution line, and writes the SoulX `prompt.json` itself (pitch found by the engine). Folder layout assumed: `.../female1/long_tones/.../f1_long_straight_a.wav`. UNVERIFIED against the real zip.
- Your own pack: a folder or zip with `pack.json` (see `engine/purrformer/voicepack.py`), then `python -m purrformer.voicepack install my-pack.zip`.
- Five extra synthetic test voices (Hollow, Warm, Bright, Choir-ish, Organ) are built in so the blend UI has something to mix today.

## Bundled voices (the goal: install the plugin and the voices are already there)
- Engine start installs every pack in `engine/bundled_voices/` (safe to repeat). So a release only has to ship that folder.
- CI: run the workflow **Build voice pack** once (Actions tab). It downloads VocalSet on GitHub's servers, cuts a small starter pack (2 short clips per singer) and publishes it as release `voices-v1`. Every plugin build after that downloads it and puts it in `bundled_voices`.
- CC BY 4.0 allows redistribution with credit; the credit line is stored with each voice and must be shown in the app's About page (TODO).
- Not bundled: the SoulX-Singer model weights (several GB). Plan: the engine downloads them on first run with a progress bar (TODO).

## First-run model download (added 2026-10-18)
Voices page has a **Download real voice model** button with a progress bar. The engine fetches the SoulX-Singer code (GitHub zip) and `model.pt` (Hugging Face, Apache-2.0 per its page) into `~/.local/share/purrformer/models` (`PURRFORMER_MODELS` to change). It resumes after a break. Endpoints: `GET /models`, `POST /models/download`. Tested against a local fake server (full download, resume, error, unsafe zip), NOT against the real URLs. Still missing: installing the Python packages (torch for ROCm, etc.), which needs your GPU machine to get right.
