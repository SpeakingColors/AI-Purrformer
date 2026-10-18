"""First-run download of the SoulX-Singer code and weights, with progress. AGPL-3.0.

Both are Apache-2.0 (checked 2026-10-18 on their pages). Files go to the models folder
(PURRFORMER_MODELS or ~/.local/share/purrformer/models). Downloads resume after a break.
This only fetches files. Python packages (torch etc.) are a separate step.
"""
import os
import shutil
import threading
import urllib.request
import zipfile
from pathlib import Path
from typing import Callable, Optional

CODE_URL = "https://github.com/Soul-AILab/SoulX-Singer/archive/refs/heads/main.zip"
WEIGHTS_URL = "https://huggingface.co/Soul-AILab/SoulX-Singer/resolve/main/model.pt"
WEIGHTS_REL = Path("pretrained_models/SoulX-Singer/model.pt")


def models_dir() -> Path:
    env = os.environ.get("PURRFORMER_MODELS")
    if env:
        return Path(env)
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / "purrformer" / "models"


def soulx_dir() -> Path:
    return models_dir() / "SoulX-Singer"


def is_installed() -> bool:
    d = soulx_dir()
    return (d / WEIGHTS_REL).is_file() and (d / "soulxsinger" / "config" / "soulxsinger.yaml").is_file()


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


def _unzip_code(zpath: Path, target: Path) -> None:
    """Extract the GitHub zip, dropping its single top folder. Refuses paths that escape the target."""
    tmp = target.with_name(target.name + ".tmp")
    shutil.rmtree(tmp, ignore_errors=True)
    with zipfile.ZipFile(zpath) as z:
        for info in z.infolist():
            parts = Path(info.filename).parts[1:]
            if not parts:
                continue
            out = (tmp.joinpath(*parts)).resolve()
            if tmp.resolve() not in out.parents and out != tmp.resolve():
                raise IOError("unsafe path in zip")
            if info.is_dir():
                out.mkdir(parents=True, exist_ok=True)
            else:
                out.parent.mkdir(parents=True, exist_ok=True)
                with z.open(info) as s, open(out, "wb") as d:
                    shutil.copyfileobj(s, d)
    keep = target / WEIGHTS_REL
    if keep.exists():  # keep downloaded weights when refreshing the code
        (tmp / WEIGHTS_REL).parent.mkdir(parents=True, exist_ok=True)
        keep.replace(tmp / WEIGHTS_REL)
    shutil.rmtree(target, ignore_errors=True)
    tmp.replace(target)


class Downloader:
    def __init__(self, code_url: str = CODE_URL, weights_url: str = WEIGHTS_URL, opener=urllib.request.urlopen):
        self.code_url, self.weights_url, self.opener = code_url, weights_url, opener
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
        """Start in the background. False if already running."""
        with self._lock:
            if self._thread and self._thread.is_alive():
                return False
            self._state = {"state": "downloading", "step": "code", "done": 0, "total": 0, "error": ""}
            self._thread = threading.Thread(target=self._run, daemon=True)
            self._thread.start()
        return True

    def join(self, timeout=None):
        if self._thread:
            self._thread.join(timeout)

    def _run(self):
        try:
            d = soulx_dir()
            prog = lambda g, t: self._set(done=g, total=t)
            if not (d / "soulxsinger" / "config" / "soulxsinger.yaml").is_file():
                self._set(step="code", done=0, total=0)
                z = models_dir() / "code.zip"
                _fetch(self.code_url, z, prog, self.opener)
                _unzip_code(z, d)
                z.unlink(missing_ok=True)
            if not (d / WEIGHTS_REL).is_file():
                self._set(step="weights", done=0, total=0)
                _fetch(self.weights_url, d / WEIGHTS_REL, prog, self.opener)
            self._set(state="done", step="")
        except Exception as exc:  # shown to the user; a rerun resumes
            self._set(state="error", error=str(exc))


downloader = Downloader()
