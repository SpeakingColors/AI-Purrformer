"""Render demo WAVs of the FX chain on a synthetic test voice. Dev tool (needs scipy).

Usage: python tools/make_demos.py OUT_DIR
The test voice is a synthetic vowel that drifts and wobbles around the melody, standing in
for an AI singer that is slightly off. It is NOT a real voice, so judge the effect, not the realism.
"""
import sys
from pathlib import Path

import numpy as np
from scipy.signal import lfilter

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from purrformer.fx import apply_fx, track_pitch  # noqa: E402
from purrformer.job import Note, SingJob  # noqa: E402
from purrformer.wav import to_wav_bytes  # noqa: E402

SR = 44100
BPM = 120
# C minor phrase (MIDI), 1 beat each, one rest
MELODY = [(0, 60), (1, 63), (2, 67), (3, 70), (4, 72), (5, 70), (6, 67), (8, 63)]
LEN = {8: 2.0}


def formant(x, f, bw):
    r = np.exp(-np.pi * bw / SR)
    th = 2 * np.pi * f / SR
    return lfilter([1 - r], [1, -2 * r * np.cos(th), r * r], x)


def make_voice(notes, seed=3):
    rng = np.random.default_rng(seed)
    n = int((max(a + b for a, b, _ in notes) * 60 / BPM + 0.3) * SR)
    t = np.arange(n) / SR
    f = np.zeros(n)
    amp = np.zeros(n)
    for start, length, pitch in notes:
        a, b = int(start * 60 / BPM * SR), int((start + length) * 60 / BPM * SR)
        f[a:b] = 440 * 2 ** ((pitch - 69) / 12)
        env = np.ones(b - a)
        r = int(0.03 * SR)
        env[:r] = np.linspace(0, 1, r)
        env[-r:] = np.linspace(1, 0, r)
        amp[a:b] = env
    # drift (slow random walk, about +-35 cents) and vibrato (5.5 Hz, about +-40 cents)
    drift = np.cumsum(rng.normal(0, 1, n))
    drift = 35 * drift / (np.abs(drift).max() + 1e-9)
    cents = drift + 40 * np.sin(2 * np.pi * 5.5 * t)
    f = f * 2 ** (cents / 1200)
    phase = np.cumsum(f / SR)
    src = 2 * (phase % 1.0) - 1
    src = src * (f > 0)
    voice = sum(formant(src, fc, bw) * g for fc, bw, g in ((800, 90, 1.0), (1150, 110, 0.6), (2800, 160, 0.3)))
    voice += 0.01 * rng.normal(size=n) * (f > 0)  # breath
    voice = voice * amp
    return 0.6 * voice / (np.abs(voice).max() + 1e-9)


def job_for(notes, **fx):
    j = SingJob(tempo_bpm=BPM, sample_rate=SR,
                notes=[Note(start_beats=a, length_beats=b, pitch=p) for a, b, p in notes])
    for k, v in fx.items():
        for kk, vv in v.items():
            setattr(getattr(j.fx, k), kk, vv)
    return j


def note_errors(x, notes):
    times, f0 = track_pitch(x, SR)
    errs = []
    for a, b, p in notes:
        s, e = a * 60 / BPM + 0.1, (a + b) * 60 / BPM - 0.1
        sel = (times >= s) & (times < e) & (f0 > 0)
        if sel.sum() < 3:
            continue
        target = 440 * 2 ** ((p - 69) / 12)
        errs.append(abs(1200 * np.log2(np.median(f0[sel]) / target)))
        # spread inside the note (vibrato/drift) in cents
    return float(np.mean(errs))


def spread(x, notes):
    times, f0 = track_pitch(x, SR)
    out = []
    for a, b, p in notes:
        s, e = a * 60 / BPM + 0.1, (a + b) * 60 / BPM - 0.1
        sel = (times >= s) & (times < e) & (f0 > 0)
        if sel.sum() >= 3:
            out.append(np.std(1200 * np.log2(f0[sel])))
    return float(np.mean(out))


def main(out_dir):
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    notes = [(a, LEN.get(a, 1.0), p) for a, p in MELODY]
    clean = make_voice(notes)
    variants = {
        "1_clean_ai_voice": {},
        "2_robot_full": dict(robot=dict(enabled=True, retune_speed=1.0, mix=1.0)),
        "3_robot_half_dry_wet": dict(robot=dict(enabled=True, retune_speed=1.0, mix=0.5)),
        "4_robot_slow_retune": dict(robot=dict(enabled=True, retune_speed=0.3, mix=1.0)),
        "5_robot_plus_dirt": dict(robot=dict(enabled=True, retune_speed=1.0, mix=1.0),
                                  dirt=dict(enabled=True, drive=0.5, bitcrush=0.3, artifacts=0.4, mix=1.0)),
        "6_dirt_only": dict(dirt=dict(enabled=True, drive=0.5, bitcrush=0.3, artifacts=0.4, mix=1.0)),
    }
    print(f"{'variant':26s} {'avg pitch error':>16s} {'pitch wobble':>14s}")
    for name, fx in variants.items():
        y = apply_fx(clean, job_for(notes, **fx))
        (out / f"{name}.wav").write_bytes(to_wav_bytes(y * 0.9, SR))
        print(f"{name:26s} {note_errors(y, notes):13.1f} c {spread(y, notes):11.1f} c")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "demos")
