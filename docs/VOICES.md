# Voices: how the bank works

## Does it come with voices?

No third-party voices. The engine ships three synthetic test timbres (sine, saw, square) so the plumbing can be tested. Everything else is added by the user.

Why: a real voice comes with rights and the consent of a person. What was checked (2026-10-18):
- LibriTTS: CC BY 4.0, read English speech at 24 kHz, not singing. Needs attribution. Source: openslr.org/60.
- VCTK: the page lists 110 English speakers reading text, but the licence text was not visible on the page, so its terms are NOT confirmed.
- No openly licensed singing dataset was confirmed.
Even where a licence allows it, cloning a real person's voice is a consent question. A voice pack is only added later if both the licence and the consent are clear.

## Adding a voice

- Import a PCM `.wav` clip, 1 to 60 seconds, up to 8 clips per voice.
- The user must pick a rights type: own voice, permission, licensed, cc0, cc-by (needs an attribution line), public-domain.
- The user must tick that they have the rights or the permission of the person. This is a prompt, not a check: the engine cannot verify it.
- Same file imported twice gives the same voice (no duplicates).
- Stored in `~/.local/share/purrformer/voices/<voice-id>/` (override with `PURRFORMER_VOICES`). The plugin Settings will let the user point it at a cloud folder.

Layout: `voice.json` plus `clip-01.wav`, `clip-02.wav`, ...

## Mixing voices

A job carries up to 4 voice slots with weights, and a `blend_mode`.

- `layer` (works now, tested): every voice sings the whole part, then the audio is mixed by weight. Like a choir or a doubled vocal. Works with any backend.
- `embedding` (experimental): the engine merges the voices into one new singer. Only a backend that implements it can do this. No engine has been confirmed to support it yet; first test is SoulX-Singer. A backend that does not support it returns an error, never a silent fallback.

## Choosing engines ("banks" of engines)

Voices belong to the user's library. The engine backend (SoulX-Singer, Seed-VC, tone test) is chosen separately in the plugin header. Engine model files are downloaded by the user, not bundled; each has its own licence (see ENGINES.md).

## API

- `GET /voices`: list (built-in and user)
- `POST /voices?name=&rights=&rights_confirmed=true&attribution=&notes=`: body is the `.wav`
- `POST /voices/{id}/clips`: add another clip
- `DELETE /voices/{id}`: user voices only
