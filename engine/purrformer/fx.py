"""Robot voice (hard retune to the MIDI notes) and dirt FX. AGPL-3.0.

Prototype in numpy so the sound can be judged before it is ported to C++ for the plugin.
Robot stage: track the pitch of the clean voice, compare with the MIDI note that
should be sounding, and shift the voice onto it with a delay-line pitch shifter.
Known limits of this prototype: the shifter moves formants along with pitch, and it
is not tuned for quality yet. Its small grain artifacts are acceptable for a robot sound.
"""
import numpy as np

from .job import SingJob


# ---------------------------------------------------------------- pitch tracking
def track_pitch(x, sr, hop_s=0.005, fmin=70.0, fmax=800.0, thresh=0.55, min_rms=0.01):
    """Return (times, f0) where f0 is 0 for unvoiced frames. Normalised autocorrelation."""
    hop = max(1, int(sr * hop_s))
    frame = int(2 ** np.ceil(np.log2(sr / fmin * 3)))
    lag_min, lag_max = int(sr / fmax), int(sr / fmin)
    n_frames = max(0, (len(x) - frame) // hop + 1)
    f0 = np.zeros(n_frames)
    win = np.hanning(frame)
    for i in range(n_frames):
        seg = x[i * hop:i * hop + frame]
        if np.sqrt(np.mean(seg ** 2)) < min_rms:
            continue
        s = (seg - seg.mean()) * win
        spec = np.fft.rfft(s, 2 * frame)
        ac = np.fft.irfft(spec * np.conj(spec))[:lag_max + 2]
        if ac[0] <= 0:
            continue
        ac = ac / ac[0]
        seg_ac = ac[lag_min:lag_max + 1]
        k = int(np.argmax(seg_ac))
        peak = seg_ac[k]
        lag = lag_min + k
        # prefer the shorter lag if it is almost as strong (avoids octave-down errors)
        half = lag // 2
        if half >= lag_min and ac[half - 1:half + 2].max() > 0.9 * peak:
            lag = half - 1 + int(np.argmax(ac[half - 1:half + 2]))
            peak = ac[lag]
        if peak < thresh or lag < 2 or lag + 1 >= len(ac):
            continue
        a, b, c = ac[lag - 1], ac[lag], ac[lag + 1]  # parabolic refinement
        den = a - 2 * b + c
        shift = 0.5 * (a - c) / den if den != 0 else 0.0
        f0[i] = sr / (lag + shift)
    times = (np.arange(n_frames) * hop + frame / 2) / sr
    return times, f0


# ---------------------------------------------------------------- pitch shifting
def delay_line_shift(x, ratio, grain=0.03, sr=44100):
    """Time-varying pitch shift by `ratio` (array per sample, 1.0 = no change).

    Two read taps on a delay line, half a grain apart, crossfaded with a raised-sine window.
    """
    n = len(x)
    g = max(16, int(grain * sr))
    d = np.cumsum(1.0 - ratio)  # delay drift per sample
    out = np.zeros(n)
    idx = np.arange(n)
    for off in (0.0, g / 2.0):
        phase = np.mod(d + off, g)
        pos = idx - phase
        w = np.sin(np.pi * phase / g) ** 2
        p0 = np.floor(pos).astype(int)
        frac = pos - p0
        a = x[np.clip(p0, 0, n - 1)]
        b = x[np.clip(p0 + 1, 0, n - 1)]
        out += (a * (1 - frac) + b * frac) * w
    return out


# ---------------------------------------------------------------- robot stage
def _target_midi_per_frame(job: SingJob, times):
    """MIDI note number sounding at each time, NaN where no note."""
    target = np.full(len(times), np.nan)
    for n in job.notes:
        a = job.beats_to_seconds(n.start_beats)
        b = job.beats_to_seconds(n.start_beats + n.length_beats)
        target[(times >= a) & (times < b)] = n.pitch
    return target


def robot(x, sr, job: SingJob):
    """Hard-retune x to the job's MIDI notes. Returns the retuned (fully wet) signal."""
    cfg = job.fx.robot
    times, f0 = track_pitch(x, sr)
    if len(times) == 0:
        return x.copy()
    target = _target_midi_per_frame(job, times)
    voiced = (f0 > 0) & ~np.isnan(target)
    cur = np.zeros(len(times))
    cur[voiced] = 69 + 12 * np.log2(f0[voiced] / 440.0)
    err = np.zeros(len(times))  # semitones to shift
    err[voiced] = target[voiced] - cur[voiced]
    # retune speed: 1.0 = jump straight to the target, lower = glide
    alpha = 0.02 + 0.98 * cfg.retune_speed ** 2
    smooth = np.zeros(len(times))
    for i in range(len(times)):
        smooth[i] = smooth[i - 1] + alpha * (err[i] - smooth[i - 1]) if i else alpha * err[i]
        if not voiced[i]:
            smooth[i] *= 0.5  # relax toward no shift on rests / unvoiced sounds
    t_samples = np.arange(len(x)) / sr
    semis = np.interp(t_samples, times, smooth)
    ratio = 2.0 ** (semis / 12.0)
    return delay_line_shift(x, ratio, sr=sr)


# ---------------------------------------------------------------- dirt stage
def saturate(x, drive):
    """Asymmetric soft clip, tube-like. drive 0..1."""
    if drive <= 0:
        return x
    k = 1.0 + 12.0 * drive
    bias = 0.15 * drive
    y = np.tanh(k * x + bias) - np.tanh(bias)
    # normalise so full-scale input never exceeds full-scale output (bias makes the two sides unequal)
    peak = max(abs(np.tanh(k + bias) - np.tanh(bias)), abs(np.tanh(-k + bias) - np.tanh(bias)))
    return y / peak


def bitcrush(x, amount):
    """Fewer bits and a lower sample rate, no anti-aliasing on purpose. amount 0..1."""
    if amount <= 0:
        return x
    bits = int(round(16 - 12 * amount))  # 16 -> 4 bits
    hold = 1 + int(round(15 * amount ** 2))  # 1 -> 16 sample hold
    levels = 2 ** (bits - 1)
    y = np.round(x * levels) / levels
    if hold > 1:
        y = np.repeat(y[::hold], hold)[:len(x)]
    return y


def artifacts(x, sr, amount, seed=7):
    """On-purpose glitches: stutters, dropouts and a little warble. amount 0..1."""
    if amount <= 0:
        return x
    rng = np.random.default_rng(seed)
    y = x.copy()
    n_events = int(amount * len(x) / sr * 6)  # up to ~6 events per second
    for _ in range(n_events):
        length = int(rng.uniform(0.02, 0.07) * sr)
        start = int(rng.integers(0, max(1, len(y) - 4 * length)))
        kind = rng.integers(0, 3)
        if kind == 0:  # stutter: repeat a slice
            slice_ = y[start:start + length].copy()
            for r in range(1, 4):
                y[start + r * length:start + (r + 1) * length] = slice_
        elif kind == 1:  # dropout
            y[start:start + length] *= 0.0
        else:  # buzzy ring-mod burst
            t = np.arange(length) / sr
            y[start:start + length] *= np.sin(2 * np.pi * rng.uniform(60, 180) * t)
    warble = 1.0 + 0.15 * amount * np.sin(2 * np.pi * 7.0 * np.arange(len(y)) / sr)
    return y * warble


def _dirt_mono(x, sr, cfg):
    y = saturate(x, cfg.drive)
    y = bitcrush(y, cfg.bitcrush)
    y = artifacts(y, sr, cfg.artifacts)
    return (1 - cfg.mix) * x + cfg.mix * y


def dirt(x, sr, job: SingJob):
    cfg = job.fx.dirt
    if x.ndim == 2:  # same glitches on both channels (seeded), so the stereo image stays stable
        return np.stack([_dirt_mono(x[:, c], sr, cfg) for c in range(x.shape[1])], axis=1)
    return _dirt_mono(x, sr, cfg)


# ---------------------------------------------------------------- supersaw stage
def supersaw(x, sr, job: SingJob):
    """Stack detuned copies of a mono voice, pan them across the stereo field, widen.

    Same idea as a supersaw oscillator, but the stack is made of the voice itself.
    Returns stereo (n, 2). The centre copy stays unshifted (delayed to line up with the others).
    """
    cfg = job.fx.supersaw
    n_copies = cfg.copies
    grain = 0.03
    align = int(grain / 2 * sr)  # shifted copies sit about half a grain late; delay the centre to match
    t = np.arange(len(x)) / sr
    offsets = np.linspace(-1.0, 1.0, n_copies)
    left = np.zeros(len(x))
    right = np.zeros(len(x))
    centre = np.concatenate([np.zeros(align), x])[:len(x)]
    for k, o in enumerate(offsets):
        pan = o * cfg.spread
        ang = (pan + 1.0) * np.pi / 4.0
        gl, gr = np.cos(ang), np.sin(ang)
        if abs(o) < 1e-9:
            y = centre
        else:
            # fixed detune plus a slow private drift (+-3 cents at full detune), each copy on its own phase
            drift = 3.0 * cfg.detune * np.sin(2 * np.pi * (0.2 + 0.07 * k) * t + 1.7 * k)
            cents = o * 50.0 * cfg.detune + drift
            y = delay_line_shift(x, 2.0 ** (cents / 1200.0), grain=grain, sr=sr)
        left += gl * y
        right += gr * y
    stack = np.stack([left, right], axis=1) / np.sqrt(n_copies / 2.0)
    dry = np.stack([centre, centre], axis=1)
    out = (1 - cfg.mix) * dry + cfg.mix * stack
    # widener: boost the side (L minus R) signal
    mid = (out[:, 0] + out[:, 1]) / 2.0
    side = (out[:, 0] - out[:, 1]) / 2.0 * (1.0 + 2.0 * cfg.widen)
    out = np.stack([mid + side, mid - side], axis=1)
    # keep loudness close to the input
    rin, rout = np.sqrt(np.mean(x ** 2)), np.sqrt(np.mean(out ** 2))
    if rout > 1e-9:
        out = out * (rin / rout)
    return out


# ---------------------------------------------------------------- chain
def apply_fx(audio, job: SingJob):
    """clean voice -> [robot, mixed with clean] -> [supersaw stack, stereo] -> [dirt]."""
    sr = job.sample_rate
    y = np.asarray(audio, dtype=np.float64)
    if len(y) == 0:
        return y
    if job.fx.robot.enabled:
        wet = robot(y, sr, job)
        m = job.fx.robot.mix
        y = (1 - m) * y + m * wet
    if job.fx.supersaw.enabled:
        y = supersaw(y, sr, job)
    if job.fx.dirt.enabled:
        y = dirt(y, sr, job)
    return np.clip(y, -1.0, 1.0)
