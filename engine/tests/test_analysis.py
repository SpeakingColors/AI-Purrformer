import numpy as np

from purrformer.analysis import analyze, detect_chords, detect_key, snap_to_scale

SR = 22050


def chord(root_midi, minor=False, seconds=2.0):
    t = np.arange(int(SR * seconds)) / SR
    third = 3 if minor else 4
    y = np.zeros_like(t)
    for semis in (0, third, 7, 12):
        f = 440.0 * 2 ** ((root_midi + semis - 69) / 12)
        for h, a in ((1, 1.0), (2, 0.5), (3, 0.3)):
            y += a * np.sin(2 * np.pi * f * h * t)
    return y * np.hanning(len(t)) ** 0.2 * 0.2


def song(chords):
    return np.concatenate([chord(r, m) for r, m in chords])


def test_key_c_major():
    x = song([(48, False), (53, False), (55, False), (48, False)])  # C F G C
    k = detect_key(x, SR)
    assert (k["tonic"], k["mode"]) == ("C", "major")
    assert k["confidence"] > 0.1


def test_key_a_minor():
    x = song([(45, True), (53, False), (52, False), (45, True)])  # Am F E Am
    k = detect_key(x, SR)
    assert (k["tonic"], k["mode"]) == ("A", "minor")


def test_chords_follow_changes():
    x = song([(48, False), (53, False), (55, False), (45, True)])
    ch = detect_chords(x, SR)
    names = [c["chord"] for c in ch]
    assert names[0] == "C" and "F" in names and "G" in names and names[-1] == "Am"
    assert ch[0]["start"] == 0.0 and all(a["end"] <= b["end"] for a, b in zip(ch, ch[1:]))


def test_silence_is_safe():
    k = detect_key(np.zeros(SR * 2), SR)
    assert k["tonic"] is None and k["confidence"] == 0.0
    assert detect_chords(np.zeros(100), SR) is not None


def test_stereo_and_short_input():
    x = chord(48)[: SR // 2]
    a = analyze(np.stack([x, x], axis=1), SR)
    assert a["key"]["scale"] is not None


def test_snap_to_scale():
    c_major = [0, 2, 4, 5, 7, 9, 11]
    assert snap_to_scale(61.0, c_major) in (60.0, 62.0)  # C#4 goes to C4 or D4
    assert snap_to_scale(64.4, c_major) == 64.0
    assert snap_to_scale(61.0, c_major, strength=0.0) == 61.0
    assert abs(snap_to_scale(64.4, c_major, 0.5) - 64.2) < 1e-9


def test_analyze_endpoint():
    from fastapi.testclient import TestClient
    from purrformer.server import app
    from purrformer.wav import to_wav_bytes
    x = song([(48, False), (53, False), (55, False), (48, False)])
    r = TestClient(app).post("/analyze", content=to_wav_bytes(x, SR))
    assert r.status_code == 200 and r.json()["key"]["tonic"] == "C"
    assert TestClient(app).post("/analyze", content=b"junk").status_code == 400
