#!/usr/bin/env bash
# Builds the AI Purrformer plugin (VST3 + Standalone) and copies the VST3 to ~/.vst3
# Run INSIDE the Ubuntu 24.04 distrobox. AGPL-3.0.
set -euo pipefail
cd "$(dirname "$0")/.."
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git pkg-config \
  libasound2-dev libcurl4-openssl-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev
cmake -S plugin -B plugin/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build plugin/build -j"$(nproc)"
mkdir -p "$HOME/.vst3"
rm -rf "$HOME/.vst3/AI Purrformer.vst3"
cp -r "plugin/build/AIPurrformer_artefacts/Release/VST3/AI Purrformer.vst3" "$HOME/.vst3/"
echo "Done. VST3 is in ~/.vst3 . Start the engine with scripts/run-engine.sh, then rescan plugins in your DAW."
