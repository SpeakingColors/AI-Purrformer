"""Turn a job into audio: render each voice, blend, then apply FX. AGPL-3.0."""
import numpy as np

from .backends.base import Backend
from .fx import apply_fx
from .job import SingJob
from .voicebank import VoiceBank, VoiceError


def render_job(job: SingJob, backend: Backend, bank: VoiceBank) -> np.ndarray:
    slots = [s for s in job.normalised_voices() if s.weight > 0]
    if job.blend_mode not in backend.blend_modes:
        raise VoiceError(f"backend '{backend.name}' does not support blend mode '{job.blend_mode}'")
    if not slots:
        audio = backend.sing(job, None)
    elif job.blend_mode == "embedding":
        audio = backend.sing_blend(job, [(bank.get(s.id), s.weight) for s in slots])
    else:  # "layer": render each voice on its own, then mix by weight. Always works.
        mix = np.zeros(0)
        for s in slots:
            a = backend.sing(job, bank.get(s.id))
            if len(a) > len(mix):
                mix = np.pad(mix, (0, len(a) - len(mix)))
            mix[:len(a)] += s.weight * a
        audio = mix
    return apply_fx(audio, job)
