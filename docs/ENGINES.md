# Engine research

Checked 2026-10-17 from each project's GitHub page. "Not stated" means the page does not say, not that it is false.

| Project | Code license | Linux | AMD / ROCm | Latency | Singing |
|---|---|---|---|---|---|
| RVC (Retrieval-based-Voice-Conversion-WebUI) | MIT | Yes (guide targets Ubuntu 24.04) | Not mentioned (CPU build offered for AMD/Intel) | Real-time GUI: ~170 ms, ~90 ms with ASIO | Not stated directly; has an "AI singer" tutorial and vocal separator |
| DDSP-SVC | MIT | Not stated (tested on Windows, CUDA 13.0, torch 2.9.1) | Not mentioned | No figure; claims low latency | Yes, built for singing voice conversion |
| Applio (RVC-based) | MIT, weights too | Yes (install scripts) | Not mentioned | Not stated | Not stated |
| so-vits-svc | AGPL-3.0 | Not stated | Not mentioned | Not stated | Yes, singing only (no TTS). ARCHIVED Nov 2023, read-only |
| Seed-VC | GPL-3.0 | Yes (Windows, Mac, Linux) | Not mentioned | Real-time: ~300 ms algorithm + ~100 ms device; 430 ms measured on an RTX 3060 laptop | Yes, zero-shot singing conversion (`app_svc.py`), no training needed. ARCHIVED Nov 2025, read-only |
| SoulX-Singer | Apache-2.0 (weights said to be under the same) | Not stated (Python 3.10, Conda) | Not mentioned | Not stated | Yes: singing synthesis from melody/MIDI AND zero-shot voice conversion (SVC released 2026-03-16) |
| ACE-Step 1.5 | MIT (weights license not stated) | Yes (.sh scripts) | YES: ROCm listed, Linux ROCm requirements file | Under 10 s per full song on an RTX 3090 | Music generation with lyrics, covers, stems, LoRA. No voice cloning described |
| YuE | Apache-2.0 incl. weights | Yes (conda, CUDA 11.8, FlashAttention 2 mandatory) | Not mentioned; FlashAttention 2 is a likely blocker | ~360 s for 30 s of audio on an RTX 4090 | Lyrics to full song with vocals, voice reference via in-context learning. Too heavy for our use |
| DiffSinger (openvpi) | Apache-2.0 | Not stated | Not mentioned | Not stated | Singing voice synthesis, not conversion. Not a fit for "replace my voice" |

## Model and voice licensing

- Code license is not the weights license. Check each pretrained model before shipping it.
- RVC says its base model was trained on VCTK and claims no copyright concerns. That is the project's own claim.
- DDSP-SVC lists no licenses for ContentVec, HubertSoft, NSF-HiFiGAN, RMVPE. Verify before bundling.
- Do not bundle anyone's voice model. Users train or supply their own.

## Still to check

- Newer conversion models (none checked yet beyond the table)
- Pitch/time correction options (offline): which libraries, which licenses
- Local lyric LLM via llama.cpp: model license, AMD support (Vulkan/ROCm)
- Whether any engine above runs on ROCm PyTorch with an RX 9070 XT (test, do not assume)

## AMD note

AMD's ROCm 6.4.4 matrix lists the RX 9070 XT with official PyTorch 2.6 support, on Ubuntu 22.04/24.04 and RHEL 9.6 only. None of the engines above document ROCm. The real test is running them on ROCm PyTorch.

## Notes from the second pass

- so-vits-svc says it is academic only, not for production, and bans illegal, religious and political use. Pretrained models may carry extra limits. Treat as reference, not a base.
- Seed-VC needs no training: a short reference clip (1 to 30 s) sets the target voice. Good for fast prototyping. Archived, so no fixes upstream. Pretrained model licenses are not stated on its page.
- GPL-3.0 and AGPL-3.0 code can be combined. The engine runs as a separate process behind localhost anyway.
- Conversion models follow the pitch of the input. Since the goal is to fix an off-key voice, pitch correction must run BEFORE conversion. This is the same for every engine above (design point, not a claim from their pages).

## Recommendation (pending ROCm test)

1. Build the engine with swappable backends.
2. First backend candidates: SoulX-Singer (newer, Apache-2.0, singing synthesis + zero-shot conversion) and Seed-VC (zero-shot, Linux, singing, archived). Test both on the RX 9070 XT.
3. Second backend: RVC or DDSP-SVC (MIT, trained voices, lighter and better for repeated use).
4. Do not build on archived projects as the only option. Plan to vendor or pin the version.
5. Gate: whichever runs on ROCm PyTorch with the RX 9070 XT wins first.
