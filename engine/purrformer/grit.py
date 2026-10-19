"""Drive, Transistor and Fuzz with a mix knob. AGPL-3.0.

Same three stages as the PurrOne synth (include/purrone/Engine.h there), in numpy:
  Drive      symmetric tanh soft clip, odd harmonics (overdriven op-amp).
  Transistor asymmetric clip with an even-harmonic bump, warm and thick (Neve-style stage).
  Fuzz       harder and lopsided; above 80% an octave-up growl built from smooth curves.
The stages run in series at 4x the sample rate (less aliasing), the level is matched to the clean
signal, a DC blocker removes the offset the asymmetry adds, and `mix` blends dry and wet.
Position: where the grit sits in the AI chain.
  before   on the recording that goes into the voice model (adds character to what the model hears)
  between  on each AI voice, before the voices are blended
  after    on the finished voice
  both     light before (40%) and full after, like PurrOne's "both"
  all      light before, medium between, full after
"""
from typing import Literal

import numpy as np
from pydantic import BaseModel, Field
from scipy.signal import resample_poly

POSITIONS = ("before", "between", "after", "both", "all")
PRE_AMT = 0.4  # same light amount as PurrOne's pre-filter stage
MID_AMT = 0.7


class GritSettings(BaseModel):
    enabled: bool = False
    drive: float = Field(default=0.0, ge=0, le=1)
    transistor: float = Field(default=0.0, ge=0, le=1)
    fuzz: float = Field(default=0.0, ge=0, le=1)
    mix: float = Field(default=0.5, ge=0, le=1)
    position: Literal["before", "between", "after", "both", "all"] = "after"

    def stages(self, where: str) -> float:
        """Amount (0..1 scale of the knobs) used at `where`; 0 means skip."""
        table = {
            "before": {"before": PRE_AMT, "both": PRE_AMT, "all": PRE_AMT},
            "between": {"between": MID_AMT, "all": MID_AMT},
            "after": {"after": 1.0, "both": 1.0, "all": 1.0},
        }[where]
        if not self.enabled or not (self.drive or self.transistor or self.fuzz):
            return 0.0
        return table.get(self.position, 0.0)


def _ss(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _asym_tanh(u, nl):
    return np.where(u >= 0.0, np.tanh(u), nl * np.tanh(u / nl))


def shaper(x, drive, xs, fuzz):
    x = np.asarray(x, dtype=np.float64)
    dn, xn, fn = _ss((drive - 0.8) * 5.0), _ss((xs - 0.8) * 5.0), _ss((fuzz - 0.8) * 5.0)
    y = x
    if drive > 0.001:
        y = np.tanh(x * (1.0 + drive * 5.0 + dn * 30.0))
        if dn > 0.0:
            y = y + dn * (np.tanh(y * (2.0 + 10.0 * dn)) - y)
    if xs > 0.001:
        nl = 1.0 - 0.3 * xs - 0.25 * xn
        y = _asym_tanh(y * (1.0 + xs * 5.0 + xn * 30.0), nl)
        y = y + (0.3 * xs + 0.25 * xn) * y * y
        if xn > 0.0:
            y2 = _asym_tanh(y * (2.0 + 10.0 * xn) + 0.15 * xn, nl)
            y = y + xn * (y2 - y)
    if fuzz > 0.001:
        fz, bf = 1.0 + fuzz * 14.0 + fn * 70.0, 0.5 * fuzz
        y = np.tanh(y * fz + bf) - np.tanh(bf)
        if fn > 0.0:
            r = np.sqrt(y * y + 0.02) - 0.1414
            y = y + fn * 0.7 * (r - 0.3)
            y = np.tanh(y * (1.0 + 0.8 * fn))
    return y


def _dc_block(y, sr):
    a = 1.0 - 2.0 * np.pi * 20.0 / sr
    out = np.empty_like(y)
    px = py = 0.0
    for i, v in enumerate(y):  # one pole, only a few hundred thousand samples
        py = v - px + a * py
        px = v
        out[i] = py
    return out


def grit(x, g: GritSettings, sr: int, where: str):
    """Apply the grit chain at one position. Returns x unchanged when it is off there."""
    amt = g.stages(where)
    x = np.asarray(x, dtype=np.float64)
    if amt <= 0 or len(x) == 0:
        return x
    d, t, f = g.drive * amt, g.transistor * amt, g.fuzz * amt
    # level match: loudness of a typical voice level sine through clean vs shaped
    ph = 2 * np.pi * np.arange(256) / 64.0
    ref = 0.9
    s = ref * np.sin(ph)
    a, b = np.sqrt(np.mean(s ** 2)), np.sqrt(np.mean(shaper(s, d, t, f) ** 2))
    make = float(np.clip(a / b, 0.25, 4.0)) if b > 1e-9 else 1.0
    # voice recordings are quiet; drive the stages from a normalised copy so the knobs mean the same on any level
    peak = float(np.max(np.abs(x)))
    norm = (0.9 / peak) if peak > 1e-6 else 1.0
    up = resample_poly(x * norm, 4, 1)
    y = shaper(up, d, t, f) * make
    y = resample_poly(y, 1, 4)[:len(x)]
    y = _dc_block(y, sr) / norm
    if len(y) < len(x):
        y = np.pad(y, (0, len(x) - len(y)))
    return (1.0 - g.mix) * x + g.mix * y
