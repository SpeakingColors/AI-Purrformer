"""Test backend: plays the notes as plain sine tones. No AI, no GPU.

Lets the plugin, the DAW round trip and the MIDI path be built and tested
before any real voice engine runs on the target machine.
"""
from typing import Optional

import numpy as np

from ..job import SingJob
from ..voicebank import Voice
from .base import Backend

_HARMONICS = {
    "sine": lambda k: 1.0 if k == 1 else 0.0,
    "saw": lambda k: 1.0 / k,
    "square": lambda k: 1.0 / k if k % 2 else 0.0,
    "hollow": lambda k: 1.0 / (k * k) if k % 2 else 0.0,
    "warm": lambda k: 0.6 ** (k - 1),
    "bright": lambda k: 1.0 / k ** 0.5,
    "choir": lambda k: (1.0 if k in (1, 2, 3) else 0.35 / (k - 2)) if k < 12 else 0.0,
    "organ": lambda k: {1: 1.0, 2: 0.8, 3: 0.5, 4: 0.6, 6: 0.3, 8: 0.2}.get(k, 0.0),
}


class ToneBackend(Backend):
    name = "tone"

    def sing(self, job: SingJob, voice: Optional[Voice] = None) -> np.ndarray:
        timbre = (voice.params.get("timbre") if voice else None) or "sine"
        amps = _HARMONICS.get(timbre, _HARMONICS["sine"])
        sr = job.sample_rate
        total = int(np.ceil(job.duration_seconds() * sr))
        out = np.zeros(total, dtype=np.float64)
        fade = int(0.005 * sr)
        for n in job.notes:
            a = int(job.beats_to_seconds(n.start_beats) * sr)
            b = min(total, int(job.beats_to_seconds(n.start_beats + n.length_beats) * sr))
            if b <= a:
                continue
            t = np.arange(b - a) / sr
            freq = 440.0 * 2.0 ** ((n.pitch - 69) / 12.0)
            seg = np.zeros(len(t))
            for k in range(1, 17):
                if freq * k >= sr / 2:
                    break
                a_k = amps(k)
                if a_k:
                    seg += a_k * np.sin(2 * np.pi * freq * k * t)
            peak = np.max(np.abs(seg))
            seg = seg / peak * (n.velocity / 127.0) * 0.5 if peak > 0 else seg
            f = min(fade, len(seg) // 2)
            if f > 0:
                ramp = np.linspace(0.0, 1.0, f)
                seg[:f] *= ramp
                seg[-f:] *= ramp[::-1]
            out[a:b] += seg
        return np.clip(out, -1.0, 1.0)
