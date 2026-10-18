#!/usr/bin/env bash
# Starts the local engine on 127.0.0.1:8765. Run INSIDE the Ubuntu distrobox. AGPL-3.0.
set -euo pipefail
cd "$(dirname "$0")/../engine"
python3 -m venv .venv 2>/dev/null || true
. .venv/bin/activate
pip install -q -r requirements.txt
exec python3 -m uvicorn purrformer.server:app --host 127.0.0.1 --port 8765
