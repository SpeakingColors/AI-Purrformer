import io
import wave

import mido
import numpy as np
import pytest
from fastapi.testclient import TestClient

from purrformer.job import SingJob, Note
from purrformer.midi import parse_midi
from purrformer.server import app

client = TestClient(app)


def make_midi(tempo_us=500000, tpb=480, lyrics=False, drum=False):
    mid = mido.MidiFile(ticks_per_beat=tpb)
    tr = mido.MidiTrack()
    mid.tracks.append(tr)
    tr.append(mido.MetaMessage("set_tempo", tempo=tempo_us, time=0))
    if lyrics:
        tr.append(mido.MetaMessage("lyrics", text="la", time=0))
    tr.append(mido.Message("note_on", note=60, velocity=90, time=0))
    tr.append(mido.Message("note_off", note=60, velocity=0, time=480))
    tr.append(mido.Message("note_on", note=64, velocity=100, time=480))
    tr.append(mido.Message("note_on", note=36, velocity=100, channel=9, time=0)) if drum else None
    tr.append(mido.Message("note_off", note=64, velocity=0, time=240))
    buf = io.BytesIO()
    mid.save(file=buf)
    return buf.getvalue()


def test_midi_notes_and_tempo():
    p = parse_midi(make_midi(tempo_us=400000))  # 150 BPM
    assert p.tempo_bpm == pytest.approx(150.0)
    assert [n.pitch for n in p.notes] == [60, 64]
    assert p.notes[0].start_beats == 0 and p.notes[0].length_beats == 1
    assert p.notes[1].start_beats == 2 and p.notes[1].length_beats == 0.5
    assert p.notes[0].velocity == 90


def test_midi_default_tempo_and_drums_skipped():
    p = parse_midi(make_midi(drum=True))
    assert p.tempo_bpm == pytest.approx(120.0)
    assert all(n.pitch != 36 for n in p.notes)


def test_midi_lyric_attached_to_first_note():
    p = parse_midi(make_midi(lyrics=True))
    assert p.notes[0].lyric == "la" and p.notes[1].lyric is None


def test_job_sorts_notes_and_duration():
    j = SingJob(tempo_bpm=120, notes=[Note(start_beats=2, length_beats=1, pitch=60),
                                      Note(start_beats=0, length_beats=1, pitch=62)])
    assert j.notes[0].start_beats == 0
    assert j.duration_seconds() == pytest.approx(1.5)  # 3 beats at 120 BPM


def test_job_rejects_bad_input():
    with pytest.raises(Exception):
        SingJob(notes=[Note(start_beats=0, length_beats=1, pitch=200)])
    with pytest.raises(Exception):
        SingJob(voices=[{"id": "a", "weight": 0}])


def test_voice_weights_normalised():
    j = SingJob(voices=[{"id": "a", "weight": 1}, {"id": "b", "weight": 3}])
    assert [v.weight for v in j.normalised_voices()] == [0.25, 0.75]


def test_health_lists_tone_backend():
    r = client.get("/health").json()
    assert {"name": "tone", "available": True} in r["backends"]


def test_sing_tone_returns_wav_with_correct_pitch():
    job = {"backend": "tone", "sample_rate": 44100, "tempo_bpm": 120,
           "notes": [{"start_beats": 0, "length_beats": 2, "pitch": 69}]}
    r = client.post("/sing", json=job)
    assert r.status_code == 200 and r.headers["content-type"] == "audio/wav"
    w = wave.open(io.BytesIO(r.content))
    assert w.getframerate() == 44100 and w.getnframes() == 44100  # 2 beats = 1 s
    x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(float)
    peak = np.argmax(np.abs(np.fft.rfft(x))) * 44100 / len(x)
    assert peak == pytest.approx(440.0, abs=2.0)


def test_unknown_backend_400_and_bad_midi_400():
    assert client.post("/sing", json={"backend": "nope"}).status_code == 400
    assert client.post("/midi/parse", content=b"garbage").status_code == 400


def test_midi_endpoint_roundtrip():
    r = client.post("/midi/parse", content=make_midi())
    assert r.status_code == 200 and len(r.json()["notes"]) == 2
