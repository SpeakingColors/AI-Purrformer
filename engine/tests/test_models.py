import io
import threading
import zipfile
from http.server import BaseHTTPRequestHandler, HTTPServer

import pytest

from purrformer import models


def make_zip():
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w") as z:
        z.writestr("SoulX-Singer-main/soulxsinger/config/soulxsinger.yaml", "a: 1")
        z.writestr("SoulX-Singer-main/README.md", "hi")
    return b.getvalue()


WEIGHTS = b"W" * 3_000_000


@pytest.fixture
def server():
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a): pass

        def do_GET(self):
            data = make_zip() if self.path == "/code.zip" else WEIGHTS
            start = 0
            rng = self.headers.get("Range")
            if rng and self.path == "/w.pt":
                start = int(rng.split("=")[1].split("-")[0])
                self.send_response(206)
            else:
                self.send_response(200)
            body = data[start:]
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
    s = HTTPServer(("127.0.0.1", 0), H)
    threading.Thread(target=s.serve_forever, daemon=True).start()
    yield f"http://127.0.0.1:{s.server_port}"
    s.shutdown()


def test_full_download(tmp_path, monkeypatch, server):
    monkeypatch.setenv("PURRFORMER_MODELS", str(tmp_path))
    d = models.Downloader(server + "/code.zip", server + "/w.pt")
    assert d.status()["state"] == "idle"
    assert d.start() is True
    d.join(20)
    st = d.status()
    assert st["state"] == "done" and st["installed"], st
    assert (tmp_path / "SoulX-Singer" / "README.md").read_text() == "hi"
    assert (tmp_path / "SoulX-Singer" / models.WEIGHTS_REL).stat().st_size == len(WEIGHTS)
    assert not list(tmp_path.rglob("*.part"))


def test_resume(tmp_path, monkeypatch, server):
    monkeypatch.setenv("PURRFORMER_MODELS", str(tmp_path))
    dest = tmp_path / "x" / "w.pt"
    dest.parent.mkdir(parents=True)
    (dest.parent / "w.pt.part").write_bytes(WEIGHTS[:1_000_000])
    seen = []
    models._fetch(server + "/w.pt", dest, lambda g, t: seen.append((g, t)))
    assert dest.read_bytes() == WEIGHTS and seen[-1] == (len(WEIGHTS), len(WEIGHTS))


def test_error_is_reported(tmp_path, monkeypatch):
    monkeypatch.setenv("PURRFORMER_MODELS", str(tmp_path))
    d = models.Downloader("http://127.0.0.1:1/nope.zip", "http://127.0.0.1:1/w.pt")
    d.start()
    d.join(20)
    st = d.status()
    assert st["state"] == "error" and st["error"]


def test_zip_slip_refused(tmp_path):
    z = tmp_path / "e.zip"
    with zipfile.ZipFile(z, "w") as zf:
        zf.writestr("top/../../evil.txt", "x")
    with pytest.raises(IOError):
        models._unzip_code(z, tmp_path / "t")
    assert not (tmp_path.parent / "evil.txt").exists()
