import io

import numpy as np
from fastapi.testclient import TestClient

from purrformer import convert
from purrformer.pitch import hz_to_midi, track_f0
from purrformer.server import app
from purrformer.voicepack import read_wav
from purrformer.wav import to_wav_bytes

SR = 24000


def sung(freqs, each=0.5):
    out = []
    for f in freqs:
        t = np.arange(int(SR * each)) / SR
        out.append(0.3 * (np.sin(2 * np.pi * f * t) + 0.4 * np.sin(4 * np.pi * f * t)))
    return np.concatenate(out)


def call(job, wav, ctx=b""):
    return TestClient(app).post("/convert", content=convert.pack(job, wav, ctx))


def out_notes(resp):
    audio, sr = read_wav(resp.content)
    m = hz_to_midi(track_f0(audio, sr, hop=480))
    return m[m > 0]


def test_pack_roundtrip():
    j, a, c = convert.unpack(convert.pack({"backend": "tone"}, b"AAAA", b"BB"))
    assert j.backend == "tone" and a == b"AAAA" and c == b"BB"


def test_follows_sung_pitch_when_not_snapping():
    r = call({"backend": "tone", "sample_rate": SR, "key": "off"}, to_wav_bytes(sung([220.0]), SR))
    assert r.status_code == 200
    m = out_notes(r)
    assert abs(np.median(m) - 57.0) < 0.3  # A3


def test_snap_to_key_pulls_off_notes():
    x = sung([277.18])  # C#4, not in C major
    r = call({"backend": "tone", "sample_rate": SR, "key": "C major", "snap_strength": 1.0}, to_wav_bytes(x, SR))
    m = np.median(out_notes(r))
    assert min(abs(m - 60), abs(m - 62)) < 0.3


def test_auto_key_from_context():
    # recording sits off the key; context audio says C major (C, F, G chords)
    from tests.test_analysis import song
    ctx = song([(48, False), (53, False), (55, False), (48, False)])
    r = call({"backend": "tone", "sample_rate": SR, "key": "auto", "snap_strength": 1.0},
             to_wav_bytes(sung([277.18]), SR), to_wav_bytes(ctx, 22050))
    m = np.median(out_notes(r))
    assert min(abs(m - 60), abs(m - 62)) < 0.3


def test_midi_guide_overrides_pitch():
    job = {"backend": "tone", "sample_rate": SR, "key": "off", "tempo_bpm": 120,
           "notes": [{"start_beats": 0, "length_beats": 2, "pitch": 64}]}
    r = call(job, to_wav_bytes(sung([220.0]), SR))
    assert abs(np.median(out_notes(r)) - 64) < 0.3


def test_errors():
    c = TestClient(app)
    assert c.post("/convert", content=b"junk").status_code == 400
    assert call({"backend": "nope"}, to_wav_bytes(sung([220.0]), SR)).status_code == 400
    assert call({"backend": "tone", "key": "H flat"}, to_wav_bytes(sung([220.0]), SR)).status_code == 400
    assert call({"backend": "tone"}, to_wav_bytes(np.zeros(100), SR)).status_code == 400
    assert call({"backend": "soulx"}, to_wav_bytes(sung([220.0]), SR)).status_code in (400, 503)


def test_manual_scale_names():
    from purrformer.analysis import scale_pitch_classes
    assert scale_pitch_classes("D", "dorian") == [0, 2, 4, 5, 7, 9, 11]  # same notes as C major
    assert scale_pitch_classes("A", "minor pentatonic") == [0, 2, 4, 7, 9]
    assert len(scale_pitch_classes("C", "chromatic")) == 12
    job = convert.ConvertJob(key="E blues")
    assert convert.resolve_scale(job, np.zeros(10), 22050) == scale_pitch_classes("E", "blues")
    import pytest
    from purrformer.voicebank import VoiceError
    with pytest.raises(VoiceError):
        convert.resolve_scale(convert.ConvertJob(key="E wobble"), np.zeros(10), 22050)
