#!/usr/bin/env bash
# Offline installer (Linux). Run from the unzipped bundle. Needs Python 3.10+ already on the machine. AGPL-3.0.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
python3 -m venv "$here/engine/.venv"
. "$here/engine/.venv/bin/activate"
pip install --no-index --find-links "$here/wheelhouse" -r "$here/engine/requirements.txt" -r "$here/engine/requirements-runtime.txt" torch torchaudio
mkdir -p "$HOME/.vst3"
rm -rf "$HOME/.vst3/AI Purrformer.vst3"
cp -r "$here/AI Purrformer.vst3" "$HOME/.vst3/"
cat > "$here/start-engine.sh" <<EOT
#!/usr/bin/env bash
cd "$here/engine" && exec "$here/engine/.venv/bin/python" -m uvicorn purrformer.server:app --host 127.0.0.1 --port 8765
EOT
chmod +x "$here/start-engine.sh"
echo "Installed. Start the engine with: $here/start-engine.sh  (no internet needed), then rescan plugins in your DAW."
