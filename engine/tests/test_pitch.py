import numpy as np

from purrformer.pitch import guide_from_notes, hz_to_midi, snap_f0, track_f0

SR = 24000


def tone(freq, seconds=1.0, amp=0.3):
    t = np.arange(int(SR * seconds)) / SR
    return amp * (np.sin(2 * np.pi * freq * t) + 0.4 * np.sin(4 * np.pi * freq * t))


def test_tracks_steady_pitch():
    f0 = track_f0(tone(220.0), SR)
    v = f0[f0 > 0]
    assert len(v) > 0.8 * len(f0)
    assert abs(np.median(v) - 220.0) < 2.0


def test_tracks_two_notes_and_silence():
    x = np.concatenate([tone(196.0, 0.6), np.zeros(SR // 2), tone(330.0, 0.6)])
    f0 = track_f0(x, SR)
    m = hz_to_midi(f0)
    assert abs(np.median(m[5:25]) - hz_to_midi(np.array([196.0]))[0]) < 0.2
    assert (f0[35:45] == 0).all()
    assert abs(np.median(m[-25:-5]) - hz_to_midi(np.array([330.0]))[0]) < 0.2


def test_silence_gives_no_pitch():
    assert (track_f0(np.zeros(SR), SR) == 0).all()


def test_snap_to_c_major():
    c_major = [0, 2, 4, 5, 7, 9, 11]
    f = np.array([277.18, 0.0, 440.0])  # C#4, none, A4
    s = hz_to_midi(snap_f0(f, c_major))
    assert s[1] == 0 and abs(s[2] - 69) < 1e-6
    assert round(s[0]) in (60, 62) and abs(s[0] - round(s[0])) < 1e-6


def test_guide_from_notes():
    g = hz_to_midi(guide_from_notes([(0.0, 0.1, 60)], 10, 0.02))
    assert (np.round(g[:5]) == 60).all() and (g[5:] == 0).all()
