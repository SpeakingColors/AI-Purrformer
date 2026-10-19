"""Pitch tracking and pitch shaping for the audio-in (convert) path. AGPL-3.0.

Own numpy code (YIN method), so no extra model, no licence question, works offline.
Made for a solo voice. Returns one pitch value per frame in Hz, 0 where there is no pitch.
"""
import numpy as np

from .analysis import snap_to_scale


def track_f0(audio: np.ndarray, sr: int, hop: int = 480, fmin: float = 70.0, fmax: float = 900.0,
             threshold: float = 0.15, min_rms: float = 0.01) -> np.ndarray:
    """YIN pitch tracker. `hop` is in samples at `sr` (SoulX uses 480 at 24 kHz = 20 ms)."""
    x = np.asarray(audio, dtype=np.float64)
    if x.ndim == 2:
        x = x.mean(axis=1)
    tau_min = max(2, int(sr / fmax))
    tau_max = int(sr / fmin)
    win = tau_max * 2
    n_frames = max(0, (len(x) - win) // hop + 1)
    pad = win // 2
    x = np.pad(x, (pad, win))  # centre frames on their time stamps
    n_frames = max(1, (len(x) - win - pad) // hop)
    f0 = np.zeros(n_frames)
    for i in range(n_frames):
        seg = x[i * hop: i * hop + win]
        if np.sqrt(np.mean(seg[:tau_max] ** 2)) < min_rms:
            continue
        # difference function through FFT autocorrelation
        n = len(seg)
        fft = np.fft.rfft(seg, 2 * n)
        acf = np.fft.irfft(fft * np.conj(fft))[:tau_max + 1]
        cs = np.concatenate([[0.0], np.cumsum(seg ** 2)])
        taus = np.arange(tau_max + 1)
        d = (cs[n - taus] - cs[0]) + (cs[n] - cs[taus]) - 2 * acf  # sum of squared differences
        d[0] = 0.0
        cm = np.ones_like(d)
        run = np.cumsum(d[1:])
        cm[1:] = d[1:] * taus[1:] / np.maximum(run, 1e-12)
        t = tau_min
        found = 0
        while t < tau_max:
            if cm[t] < threshold:
                while t + 1 < tau_max and cm[t + 1] < cm[t]:
                    t += 1
                found = t
                break
            t += 1
        if not found:
            continue
        # parabolic refinement
        a, b, c = cm[found - 1], cm[found], cm[found + 1]
        den = a - 2 * b + c
        shift = 0.5 * (a - c) / den if abs(den) > 1e-12 else 0.0
        f0[i] = sr / (found + shift)
    return _despike(f0)


def _despike(f0: np.ndarray) -> np.ndarray:
    """Remove single-frame octave jumps and lone voiced frames."""
    out = f0.copy()
    for i in range(1, len(f0) - 1):
        if f0[i] > 0 and f0[i - 1] > 0 and f0[i + 1] > 0:
            ref = np.sqrt(f0[i - 1] * f0[i + 1])
            if abs(12 * np.log2(f0[i] / ref)) > 6:
                out[i] = ref
        elif f0[i] > 0 and f0[i - 1] == 0 and f0[i + 1] == 0:
            out[i] = 0
    return out


def hz_to_midi(f: np.ndarray) -> np.ndarray:
    f = np.asarray(f, dtype=np.float64)
    return np.where(f > 0, 69.0 + 12.0 * np.log2(np.maximum(f, 1e-9) / 440.0), 0.0)


def midi_to_hz(m: np.ndarray) -> np.ndarray:
    m = np.asarray(m, dtype=np.float64)
    return np.where(m > 0, 440.0 * 2.0 ** ((m - 69.0) / 12.0), 0.0)


def snap_f0(f0: np.ndarray, scale: list, strength: float = 1.0) -> np.ndarray:
    """Pull a pitch curve toward the notes of a scale. strength 0 = untouched, 1 = on the notes."""
    midi = hz_to_midi(f0)
    out = np.array([snap_to_scale(m, scale, strength) if m > 0 else 0.0 for m in midi])
    return midi_to_hz(out)


def guide_from_notes(notes: list, n_frames: int, frame_s: float, blend: float = 1.0,
                     f0: np.ndarray = None) -> np.ndarray:
    """Pitch curve from MIDI notes: [(start_s, end_s, midi_pitch)]. With `f0` and blend < 1 the
    sung pitch is mixed toward the MIDI pitch (in semitones). Frames with no note keep the sung pitch."""
    sung = hz_to_midi(f0) if f0 is not None else np.zeros(n_frames)
    out = sung.copy()
    for s, e, p in notes:
        a, b = int(s / frame_s), min(n_frames, int(np.ceil(e / frame_s)))
        for i in range(max(0, a), b):
            out[i] = p if sung[i] <= 0 else sung[i] + (p - sung[i]) * blend
            if f0 is None or sung[i] <= 0:
                out[i] = p
    return midi_to_hz(out)
