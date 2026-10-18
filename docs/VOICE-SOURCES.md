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
