import io
import wave

import numpy as np
import pytest
from fastapi.testclient import TestClient

from purrformer import server
from purrformer.backends import get as get_backend
from purrformer.job import SingJob, Note
from purrformer.render import render_job
from purrformer.voicebank import VoiceBank, VoiceError


def wav_bytes(secs=2.0, sr=16000):
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes((np.sin(np.arange(int(secs * sr)) * 0.05) * 8000).astype("<i2").tobytes())
    return buf.getvalue()


@pytest.fixture
def bank(tmp_path):
    return VoiceBank(tmp_path / "voices")


@pytest.fixture
def client(bank, monkeypatch):
    monkeypatch.setattr(server, "bank", bank)
    return TestClient(server.app)


def test_builtins_always_listed_and_no_folder_created(bank):
    ids = [v.id for v in bank.all_voices()]
    assert {"test-sine", "test-saw", "test-square"} <= set(ids)
    assert not bank.root.exists()


def test_import_needs_confirmed_rights(bank):
    with pytest.raises(VoiceError):
        bank.import_voice("Me", wav_bytes(), "own-voice", rights_confirmed=False)
    with pytest.raises(VoiceError):
        bank.import_voice("Me", wav_bytes(), "made-up", rights_confirmed=True)
    with pytest.raises(VoiceError):
        bank.import_voice("Me", wav_bytes(), "cc-by", rights_confirmed=True)  # no attribution


def test_import_validates_clip(bank):
    with pytest.raises(VoiceError):
        bank.import_voice("Me", b"not a wav", "own-voice", True)
    with pytest.raises(VoiceError):
        bank.import_voice("Me", wav_bytes(secs=0.3), "own-voice", True)
    with pytest.raises(VoiceError):
        bank.import_voice("Me", wav_bytes(secs=61), "own-voice", True)


def test_import_roundtrip_dedupe_delete(bank):
    v = bank.import_voice("My Voice!", wav_bytes(), "own-voice", True)
    assert v.id.startswith("my-voice-") and v.kind == "user" and v.clips[0]["duration_s"] == 2.0
    assert bank.import_voice("My Voice!", wav_bytes(), "own-voice", True).id == v.id  # same bytes, same voice
    assert bank.get(v.id).name == "My Voice!"
    assert bank.clip_paths(v.id)[0].is_file()
    bank.delete(v.id)
    with pytest.raises(VoiceError):
        bank.get(v.id)


def test_cannot_touch_builtins_or_escape_folder(bank):
    with pytest.raises(VoiceError):
        bank.delete("test-saw")
    with pytest.raises(VoiceError):
        bank.add_clip("test-saw", wav_bytes())
    for bad in ("../etc", "a/b", "..", "UPPER", ""):
        with pytest.raises(VoiceError):
            bank.get(bad)


def test_add_clip_limit(bank):
    v = bank.import_voice("Me", wav_bytes(), "own-voice", True)
    for i in range(7):
        v = bank.add_clip(v.id, wav_bytes(secs=2.0 + i / 10))
    assert len(v.clips) == 8
    with pytest.raises(VoiceError):
        bank.add_clip(v.id, wav_bytes())


def job(**kw):
    return SingJob(tempo_bpm=120, notes=[Note(start_beats=0, length_beats=2, pitch=57)], **kw)


def test_layer_blend_is_the_weighted_mix_of_separate_renders(bank):
    tone = get_backend("tone")
    j = job(voices=[{"id": "test-sine", "weight": 3}, {"id": "test-saw", "weight": 1}])
    got = render_job(j, tone, bank)
    a = tone.sing(j, bank.get("test-sine"))
    b = tone.sing(j, bank.get("test-saw"))
    assert np.allclose(got, 0.75 * a + 0.25 * b)


def test_voices_sound_different(bank):
    tone = get_backend("tone")

    def centroid(x):
        s = np.abs(np.fft.rfft(x))
        return float((s * np.arange(len(s))).sum() / s.sum())

    sine = render_job(job(voices=[{"id": "test-sine", "weight": 1}]), tone, bank)
    saw = render_job(job(voices=[{"id": "test-saw", "weight": 1}]), tone, bank)
    assert centroid(saw) > 2 * centroid(sine)


def test_no_voices_still_works(bank):
    assert len(render_job(job(), get_backend("tone"), bank)) > 0


def test_unknown_voice_and_unsupported_blend_mode(client):
    base = {"notes": [{"start_beats": 0, "length_beats": 1, "pitch": 60}]}
    r = client.post("/sing", json={**base, "voices": [{"id": "nope", "weight": 1}]})
    assert r.status_code == 400
    r = client.post("/sing", json={**base, "blend_mode": "embedding", "voices": [{"id": "test-saw", "weight": 1}]})
    assert r.status_code == 400 and "blend mode" in r.json()["detail"]


def test_api_flow(client):
    assert any(v["id"] == "test-sine" for v in client.get("/voices").json()["voices"])
    r = client.post("/voices?name=Me&rights=own-voice", content=wav_bytes())
    assert r.status_code == 400  # rights not confirmed
    r = client.post("/voices?name=Me&rights=own-voice&rights_confirmed=true", content=wav_bytes())
    assert r.status_code == 200
    vid = r.json()["id"]
    assert "file" not in str(r.json()["clips"])  # no file names or paths leak
    body = {"notes": [{"start_beats": 0, "length_beats": 1, "pitch": 60}],
            "voices": [{"id": "test-saw", "weight": 1}, {"id": "test-square", "weight": 1}]}
    assert client.post("/sing", json=body).status_code == 200
    assert client.delete(f"/voices/{vid}").status_code == 200
    assert client.delete("/voices/test-saw").status_code == 400
