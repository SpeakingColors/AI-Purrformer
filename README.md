# AI Purrformer

<img src="assets/icon.svg" width="96" align="right" alt="icon">

Free, open source AI singing voice tool for Linux. Part of the cat plugin suite by Studio 87.

**Goal:** sing badly, get a much better voice back: on time, on key, in tune with the song. Bonus: a lyric notepad and helper.

**Status:** research and planning. No working code yet.

## How it works (planned)

1. A JUCE VST3/CLAP plugin takes lyrics and a MIDI melody (or a clip) from the DAW.
2. It sends the clip to a local engine (Python, runs on your GPU in a container).
3. The engine sings your lyrics on the melody you give it (MIDI or a guide vocal), in the chosen voice. Your own pitch is not used.
4. The result comes back and lands on the track.

This is offline processing: render a clip, get audio back. It is not live. Live voice replacement is a research item, not a promise.

## Target setup

- OS: Bazzite KDE (immutable Fedora), no dual boot
- DAWs: Bitwig, Studio One
- GPU: AMD RX 9070 XT (ROCm, not CUDA)
- Engine runs in an Ubuntu container (Distrobox); plugin talks to it over localhost

## Layout

- `plugin/` JUCE plugin (AGPL-3.0)
- `engine/` Python local server
- `lyrics/` lyric notepad and helper
- `flatpak/` Flathub packaging (later)
- `docs/` research, architecture, install notes, roadmap

## Docs

- [Engine research](docs/ENGINES.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Install on Bazzite](docs/INSTALL-BAZZITE.md)
- [Voices and mixing](docs/VOICES.md)
- [Where the melody comes from](docs/MELODY-SOURCES.md)
- [FX, inferred accompaniment, lyric AI](docs/FX-AND-LYRICS.md)
- [Inspiration and borrowable ideas](docs/INSPIRATION.md)
- [Roadmap](docs/ROADMAP.md)

## License

Code: AGPL-3.0 (see `LICENSE`). Voice models and weights have their own terms; see `docs/ENGINES.md`. Only train and convert with voices you have the rights to use.

## Support

<p align="center">
  <a href="https://ko-fi.com/studio87"><img src="https://img.shields.io/badge/Support_Studio_87-on_Ko--fi-FF5E5B?style=for-the-badge&logo=kofi&logoColor=white" alt="Support Studio 87 on Ko-fi"></a>
</p>

☕ Free and open source, made by one person. If it helps your music, a coffee on [Ko-fi](https://ko-fi.com/studio87) keeps it coming. 💜
