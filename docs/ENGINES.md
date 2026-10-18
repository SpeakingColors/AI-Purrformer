# Engine research

Checked 2026-10-17 from each project's GitHub page. "Not stated" means the page does not say, not that it is false.

| Project | Code license | Linux | AMD / ROCm | Latency | Singing |
|---|---|---|---|---|---|
| RVC (Retrieval-based-Voice-Conversion-WebUI) | MIT | Yes (guide targets Ubuntu 24.04) | Not mentioned (CPU build offered for AMD/Intel) | Real-time GUI: ~170 ms, ~90 ms with ASIO | Not stated directly; has an "AI singer" tutorial and vocal separator |
| DDSP-SVC | MIT | Not stated (tested on Windows, CUDA 13.0, torch 2.9.1) | Not mentioned | No figure; claims low latency | Yes, built for singing voice conversion |
| Applio (RVC-based) | MIT, weights too | Yes (install scripts) | Not mentioned | Not stated | Not stated |

## Model and voice licensing

- Code license is not the weights license. Check each pretrained model before shipping it.
- RVC says its base model was trained on VCTK and claims no copyright concerns. That is the project's own claim.
- DDSP-SVC lists no licenses for ContentVec, HubertSoft, NSF-HiFiGAN, RMVPE. Verify before bundling.
- Do not bundle anyone's voice model. Users train or supply their own.

## Still to check

- so-vits-svc (fork status, license)
- DiffSinger (synthesis from notes, not conversion)
- Seed-VC and other newer conversion models
- Pitch/time correction options (offline): which libraries, which licenses
- Local lyric LLM via llama.cpp: model license, AMD support (Vulkan/ROCm)
- Whether any engine above runs on ROCm PyTorch with an RX 9070 XT (test, do not assume)

## AMD note

AMD's ROCm 6.4.4 matrix lists the RX 9070 XT with official PyTorch 2.6 support, on Ubuntu 22.04/24.04 and RHEL 9.6 only. None of the engines above document ROCm. The real test is running them on ROCm PyTorch.
