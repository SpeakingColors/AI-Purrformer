"""Test backend: plays the notes as plain sine tones. No AI, no GPU.

Lets the plugin, the DAW round trip and the MIDI path be built and tested
before any real voice engine runs on the target machine.
"""
import numpy as np

from ..job import SingJob
from .base import Backend


class ToneBackend(Backend):
    name = "tone"

    def sing(self, job: SingJob) -> np.ndarray:
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
            seg = np.sin(2 * np.pi * freq * t) * (n.velocity / 127.0) * 0.5
            f = min(fade, len(seg) // 2)
            if f > 0:
                ramp = np.linspace(0.0, 1.0, f)
                seg[:f] *= ramp
                seg[-f:] *= ramp[::-1]
            out[a:b] += seg
        return np.clip(out, -1.0, 1.0)
