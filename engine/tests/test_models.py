import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

import pytest

from purrformer import models

WEIGHTS = b"W" * 3_000_000


@pytest.fixture
def server():
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a): pass

        def do_GET(self):
            start = 0
            rng = self.headers.get("Range")
            if rng:
                start = int(rng.split("=")[1].split("-")[0])
                self.send_response(206)
            else:
                self.send_response(200)
            body = WEIGHTS[start:]
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
    s = HTTPServer(("127.0.0.1", 0), H)
    threading.Thread(target=s.serve_forever, daemon=True).start()
    yield f"http://127.0.0.1:{s.server_port}"
    s.shutdown()


@pytest.fixture(autouse=True)
def isolate(tmp_path, monkeypatch):
    monkeypatch.setenv("PURRFORMER_MODELS", str(tmp_path / "m"))
    monkeypatch.delenv("PURRFORMER_SOULX_DIR", raising=False)
    monkeypatch.setattr(models, "ENGINE_DIR", tmp_path / "engine")  # no bundled weights in the test


def test_vendored_code_is_found_in_repo(monkeypatch):
    monkeypatch.undo()  # real ENGINE_DIR
    assert models.code_dir() is not None and (models.code_dir() / models.CONFIG_REL).is_file()


def test_bundled_weights_win_and_need_no_network(tmp_path):
    w = tmp_path / "engine" / "models" / "SoulX-Singer" / "model.pt"
    w.parent.mkdir(parents=True)
    w.write_bytes(b"x")
    assert models.weights_path() == w
    d = models.Downloader("http://127.0.0.1:1/never")  # would fail if it tried
    d.start(); d.join(5)
    assert d.status()["state"] == "done"


def test_repair_download(tmp_path, server):
    d = models.Downloader(server + "/w.pt")
    assert models.weights_path() is None and d.status()["state"] == "idle"
    assert d.start() is True
    d.join(20)
    assert d.status()["state"] == "done"
    assert models.weights_path().stat().st_size == len(WEIGHTS)
    assert not list(tmp_path.rglob("*.part"))


def test_resume(tmp_path, server):
    dest = tmp_path / "x" / "w.pt"
    dest.parent.mkdir(parents=True)
    (dest.parent / "w.pt.part").write_bytes(WEIGHTS[:1_000_000])
    seen = []
    models._fetch(server + "/w.pt", dest, lambda g, t: seen.append((g, t)))
    assert dest.read_bytes() == WEIGHTS and seen[-1] == (len(WEIGHTS), len(WEIGHTS))


def test_error_is_reported():
    d = models.Downloader("http://127.0.0.1:1/w.pt")
    d.start(); d.join(20)
    st = d.status()
    assert st["state"] == "error" and st["error"]
