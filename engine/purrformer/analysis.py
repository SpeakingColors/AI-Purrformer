"""Key and chord detection from audio. AGPL-3.0.

Plain numpy, no downloads, no extra models, so it works offline and installs small.
Method: pitch-class profile (chroma) from a short-time spectrum, then the Krumhansl-Kessler key
profiles for the key, and major/minor triad templates with a stay-in-the-chord penalty (Viterbi)
for the chords. This is a good guess, not a perfect transcription: the key is usually right on
music with clear harmony, chords are rougher. Heavy drums or no harmony lower the confidence.
"""
from typing import Dict, List, Optional

import numpy as np

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

# Krumhansl-Kessler probe-tone profiles (tonic first).
_MAJOR = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
_MINOR = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])
MAJOR_STEPS = (0, 2, 4, 5, 7, 9, 11)
MINOR_STEPS = (0, 2, 3, 5, 7, 8, 10)  # natural minor


def _mono(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.float64)
    return x.mean(axis=1) if x.ndim == 2 else x


def chroma_frames(audio: np.ndarray, sr: int, frame_s: float = 0.25, fmin: float = 55.0,
                  fmax: float = 2000.0):
    """-> (frame start times in seconds, chroma matrix [frames, 12]); rows are L1 normalised."""
    x = _mono(audio)
    n = int(2 ** np.ceil(np.log2(max(2048, sr * 0.19))))  # about 190 ms window (8192 at 44.1k)
    hop = max(1, int(sr * frame_s))
    if len(x) < n:
        x = np.pad(x, (0, n - len(x)))
    win = np.hanning(n)
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    sel = (freqs >= fmin) & (freqs <= fmax)
    midi = 69.0 + 12.0 * np.log2(freqs[sel] / 440.0)
    pc = np.mod(np.round(midi).astype(int), 12)
    # weight low bins less so one loud bass note does not decide the key
    weight = 1.0 / np.sqrt(np.maximum(freqs[sel], 1.0) / 110.0)
    starts = np.arange(0, len(x) - n + 1, hop)
    out = np.zeros((len(starts), 12))
    for i, s in enumerate(starts):
        mag = np.abs(np.fft.rfft(x[s:s + n] * win))[sel]
        mag = np.log1p(mag * 50.0) * weight  # compress so strong partials do not swamp
        out[i] = np.bincount(pc, weights=mag, minlength=12)
    tot = out.sum(axis=1, keepdims=True)
    out = np.divide(out, tot, out=np.zeros_like(out), where=tot > 1e-12)
    return starts / sr, out


def _corr(a: np.ndarray, b: np.ndarray) -> float:
    a = a - a.mean()
    b = b - b.mean()
    d = np.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / d) if d > 1e-12 else 0.0


def detect_key(audio: np.ndarray, sr: int) -> Dict:
    """Best key as {'tonic': 'A', 'mode': 'minor', 'confidence': 0..1, 'scale': [pitch classes],
    'runner_up': 'C major'}. Confidence is how far the best key beats the second best."""
    _, ch = chroma_frames(audio, sr)
    prof = ch.mean(axis=0) if len(ch) else np.zeros(12)
    if prof.sum() <= 1e-9:
        return {"tonic": None, "mode": None, "confidence": 0.0, "scale": [], "runner_up": None}
    scores = []
    for tonic in range(12):
        scores.append((_corr(prof, np.roll(_MAJOR, tonic)), tonic, "major"))
        scores.append((_corr(prof, np.roll(_MINOR, tonic)), tonic, "minor"))
    scores.sort(reverse=True)
    best, second = scores[0], scores[1]
    steps = MAJOR_STEPS if best[2] == "major" else MINOR_STEPS
    conf = float(np.clip((best[0] - second[0]) / 0.25, 0.0, 1.0)) * float(np.clip(best[0] / 0.8, 0.0, 1.0))
    return {
        "tonic": NOTE_NAMES[best[1]], "mode": best[2], "confidence": round(conf, 3),
        "scale": sorted((best[1] + s) % 12 for s in steps),
        "runner_up": f"{NOTE_NAMES[second[1]]} {second[2]}",
    }


def _templates():
    names, mats = [], []
    for root in range(12):
        for quality, third in (("", 4), ("m", 3)):
            t = np.zeros(12)
            t[root] = 1.0
            t[(root + third) % 12] = 0.9
            t[(root + 7) % 12] = 0.8
            names.append(NOTE_NAMES[root] + quality)
            mats.append(t / np.linalg.norm(t))
    return names, np.array(mats)


def detect_chords(audio: np.ndarray, sr: int, stay: float = 0.15) -> List[Dict]:
    """Chord per stretch of time: [{'start': s, 'end': s, 'chord': 'Am'}]. Triads only (major or
    minor); extended chords map to the nearest triad. `stay` is the cost of changing chord."""
    times, ch = chroma_frames(audio, sr)
    if len(ch) == 0:
        return []
    names, tpl = _templates()
    emit = np.zeros((len(ch), len(names)))
    for i, row in enumerate(ch):
        nrm = np.linalg.norm(row)
        emit[i] = tpl @ (row / nrm) if nrm > 1e-12 else 0.0
    # Viterbi with a flat penalty for changing chord
    n, k = emit.shape
    score = emit[0].copy()
    back = np.zeros((n, k), dtype=int)
    for i in range(1, n):
        best_prev = int(np.argmax(score))
        stay_s = score
        move_s = score[best_prev] - stay
        take_move = move_s > stay_s
        back[i] = np.where(take_move, best_prev, np.arange(k))
        score = np.where(take_move, move_s, stay_s) + emit[i]
    path = np.zeros(n, dtype=int)
    path[-1] = int(np.argmax(score))
    for i in range(n - 1, 0, -1):
        path[i - 1] = back[i, path[i]]
    hop = float(times[1] - times[0]) if len(times) > 1 else 0.25
    out: List[Dict] = []
    for i, c in enumerate(path):
        if out and out[-1]["chord"] == names[c]:
            out[-1]["end"] = round(float(times[i]) + hop, 3)
        else:
            out.append({"start": round(float(times[i]), 3), "end": round(float(times[i]) + hop, 3),
                        "chord": names[c]})
    return out


def snap_to_scale(midi_pitch: float, scale: List[int], strength: float = 1.0) -> float:
    """Move a (possibly fractional) MIDI pitch toward the nearest scale note.
    strength 0 = untouched, 1 = exactly on a scale note."""
    if not scale:
        return midi_pitch
    base = int(np.floor(midi_pitch / 12.0)) * 12
    cands = [base + p + o for p in scale for o in (-12, 0, 12)]
    nearest = min(cands, key=lambda c: abs(c - midi_pitch))
    return midi_pitch + (nearest - midi_pitch) * float(np.clip(strength, 0.0, 1.0))


def analyze(audio: np.ndarray, sr: int, seconds_limit: Optional[float] = 600.0) -> Dict:
    x = _mono(audio)
    if seconds_limit and len(x) > seconds_limit * sr:
        x = x[: int(seconds_limit * sr)]
    return {"seconds": round(len(x) / sr, 2), "key": detect_key(x, sr), "chords": detect_chords(x, sr)}
