"""Where the SoulX-Singer code and weights live, and a repair download. AGPL-3.0.

Everything needed to sing ships INSIDE the installer, so it works offline:
  code     engine/third_party/SoulX-Singer/soulxsinger   (vendored, Apache-2.0)
  weights  engine/models/SoulX-Singer/model.pt            (put there by the packaging workflow)
The download below is only a repair button for when the weights file is missing. It resumes after a break.
Lookup order for both: PURRFORMER_SOULX_DIR, the bundled folders, then the user models folder.
"""
import os
import sys
import threading
import urllib.request
from pathlib import Path
from typing import Callable, Optional

WEIGHTS_URL = "https://huggingface.co/Soul-AILab/SoulX-Singer/resolve/main/model.pt"
WEIGHTS_REL = Path("pretrained_models/SoulX-Singer/model.pt")
CONFIG_REL = Path("soulxsinger/config/soulxsinger.yaml")
ENGINE_DIR = Path(__file__).resolve().parents[1]


def setup_env() -> None:
    """Use the bundled NLTK data (g2p_en wants it) so nothing is fetched from the internet."""
    d = ENGINE_DIR / "nltk_data"
    if d.is_dir():
        os.environ.setdefault("NLTK_DATA", str(d))


def data_root() -> Path:
    """User data folder: <app data>/Studio 87/AI Purrformer (Windows %APPDATA%, macOS Application
    Support, Linux XDG data home)."""
    if os.name == "nt":
        base = os.environ.get("APPDATA") or str(Path.home() / "AppData" / "Roaming")
    elif sys.platform == "darwin":
        base = str(Path.home() / "Library" / "Application Support")
    else:
        base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / "Studio 87" / "AI Purrformer"


def models_dir() -> Path:
    env = os.environ.get("PURRFORMER_MODELS")
    if env:
        return Path(env)
    return data_root() / "models"


def code_dir() -> Optional[Path]:
    """Folder that contains the `soulxsinger` package."""
    env = os.environ.get("PURRFORMER_SOULX_DIR")
    for d in ([Path(env)] if env else []) + [ENGINE_DIR / "third_party" / "SoulX-Singer"]:
        if (d / CONFIG_REL).is_file():
            return d
    return None


def weights_path() -> Optional[Path]:
    env = os.environ.get("PURRFORMER_SOULX_DIR")
    for p in ([Path(env) / WEIGHTS_REL] if env else []) + [
            ENGINE_DIR / "models" / "SoulX-Singer" / "model.pt",
            models_dir() / "model.pt"]:
        if p.is_file():
            return p
    return None


def svc_weights_path() -> Optional[Path]:
    """Voice-conversion (audio in) weights. File name to be confirmed on the first GPU test."""
    env = os.environ.get("PURRFORMER_SOULX_DIR")
    for p in ([Path(env) / "pretrained_models/SoulX-Singer/model-svc.pt"] if env else []) + [
            ENGINE_DIR / "models" / "SoulX-Singer" / "model-svc.pt",
            models_dir() / "model-svc.pt"]:
        if p.is_file():
            return p
    return None


def is_installed() -> bool:
    return code_dir() is not None and weights_path() is not None


def _fetch(url: str, dest: Path, progress: Callable[[int, int], None], opener=urllib.request.urlopen) -> None:
    """Download to dest via dest.part, resuming if a part file exists. Checks the final size."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    part = dest.with_name(dest.name + ".part")
    have = part.stat().st_size if part.exists() else 0
    req = urllib.request.Request(url, headers={"User-Agent": "ai-purrformer"})
    if have:
        req.add_header("Range", f"bytes={have}-")
    with opener(req, timeout=60) as r:
        status = getattr(r, "status", 200)
        length = int(r.headers.get("Content-Length") or 0)
        if have and status != 206:  # server ignored the range: start over
            have = 0
        total = have + length if length else 0
        with open(part, "ab" if have else "wb") as f:
            got = have
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                f.write(chunk)
                got += len(chunk)
                progress(got, total)
    if total and part.stat().st_size != total:
        raise IOError("download ended early, run it again to resume")
    part.replace(dest)


class Downloader:
    """Repair only: fetches model.pt into the user models folder if no copy is found."""

    def __init__(self, weights_url: str = WEIGHTS_URL, opener=urllib.request.urlopen):
        self.weights_url, self.opener = weights_url, opener
        self._lock = threading.Lock()
        self._state = {"state": "idle", "step": "", "done": 0, "total": 0, "error": ""}
        self._thread: Optional[threading.Thread] = None

    def status(self) -> dict:
        with self._lock:
            s = dict(self._state)
        s["installed"] = is_installed()
        if s["state"] == "idle" and s["installed"]:
            s["state"] = "done"
        return s

    def _set(self, **kw):
        with self._lock:
            self._state.update(kw)

    def start(self) -> bool:
        with self._lock:
            if self._thread and self._thread.is_alive():
                return False
            self._state = {"state": "downloading", "step": "weights", "done": 0, "total": 0, "error": ""}
            self._thread = threading.Thread(target=self._run, daemon=True)
            self._thread.start()
        return True

    def join(self, timeout=None):
        if self._thread:
            self._thread.join(timeout)

    def _run(self):
        try:
            if weights_path() is None:
                _fetch(self.weights_url, models_dir() / "model.pt", lambda g, t: self._set(done=g, total=t), self.opener)
            self._set(state="done", step="")
        except Exception as exc:  # shown to the user; a rerun resumes
            self._set(state="error", error=str(exc))


downloader = Downloader()
