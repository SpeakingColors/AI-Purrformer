from purrformer.backends.soulx_meta import job_to_segments, split_words
from purrformer.job import Note, SingJob

FAKE = {"hello": ["HH", "AH0", "L", "OW1"], "la": ["L", "AA1"], "world": ["W", "ER1", "L", "D"]}
g2p = lambda w: FAKE.get(w, ["AH0"])


def job(notes, lyrics=None, bpm=120):
    return SingJob(tempo_bpm=bpm, lyrics=lyrics, notes=notes)


def fields(seg):
    return {k: seg[k].split() for k in ("duration", "text", "phoneme", "note_pitch", "note_type")}


def test_basic_words_and_rests():
    j = job([Note(start_beats=0, length_beats=1, pitch=60), Note(start_beats=2, length_beats=1, pitch=62)], "hello world")
    (seg,) = job_to_segments(j, g2p)
    f = fields(seg)
    assert f["text"] == ["<SP>", "hello", "<SP>", "world", "<SP>"]
    assert f["phoneme"][1] == "en_HH-AH0-L-OW1" and f["phoneme"][3] == "en_W-ER1-L-D"
    assert f["note_pitch"] == ["0", "60", "0", "62", "0"]
    assert f["note_type"] == ["1", "2", "1", "2", "1"]
    # all five token lists the same length, durations add up to the segment
    assert len({len(v) for v in f.values()}) == 1
    assert abs(sum(map(float, f["duration"])) * 1000 - (seg["time"][1] - seg["time"][0])) < 30


def test_hold_when_lyrics_run_out():
    j = job([Note(start_beats=i, length_beats=1, pitch=60 + i) for i in range(3)], "hello")
    (seg,) = job_to_segments(j, g2p)
    f = fields(seg)
    assert f["note_type"] == ["1", "2", "3", "3", "1"]
    assert f["phoneme"][1] == f["phoneme"][2] == f["phoneme"][3]


def test_no_lyrics_sings_la():
    (seg,) = job_to_segments(job([Note(start_beats=0, length_beats=1, pitch=60)]), g2p)
    assert fields(seg)["text"][1] == "la"


def test_per_note_lyric_wins():
    j = job([Note(start_beats=0, length_beats=1, pitch=60, lyric="world")], "hello")
    (seg,) = job_to_segments(j, g2p)
    assert fields(seg)["text"][1] == "world"


def test_overlap_trimmed_and_slivers_dropped():
    j = job([Note(start_beats=0, length_beats=2, pitch=60), Note(start_beats=1, length_beats=1, pitch=62),
             Note(start_beats=1.01, length_beats=0.01, pitch=64)], "hello world")
    (seg,) = job_to_segments(j, g2p)
    assert fields(seg)["note_pitch"].count("0") == 2  # only the two end rests
    assert all(float(d) > 0 for d in fields(seg)["duration"])


def test_long_job_is_split():
    notes = [Note(start_beats=i * 2, length_beats=1, pitch=60) for i in range(40)]
    segs = job_to_segments(job(notes), g2p, max_segment_s=8)
    assert len(segs) > 1
    assert segs[0]["time"][1] <= segs[1]["time"][0] + 200


def test_empty():
    assert job_to_segments(job([]), g2p) == []
    assert split_words("Don't stop, believin'!") == ["Don't", "stop", "believin"]


def test_soulx_backend_registered_but_unavailable_without_setup(monkeypatch):
    from purrformer import backends
    monkeypatch.delenv("PURRFORMER_SOULX_DIR", raising=False)
    b = backends.get("soulx")
    assert b is not None and b.available() is False
    import pytest
    from purrformer.voicebank import VoiceError
    with pytest.raises(VoiceError):
        b.sing(SingJob(notes=[Note(start_beats=0, length_beats=1, pitch=60)]), None)
