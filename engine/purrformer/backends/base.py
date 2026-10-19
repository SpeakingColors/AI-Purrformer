"""Backend interface. Every voice engine (SoulX-Singer, Seed-VC, ...) implements this."""
from abc import ABC, abstractmethod
from typing import Optional

import numpy as np

from ..job import SingJob
from ..voicebank import Voice


class Backend(ABC):
    name: str = "base"
    blend_modes: tuple = ("layer",)  # add "embedding" when sing_blend is implemented

    def available(self) -> bool:
        """True if the engine can run here (deps installed, GPU found, ...)."""
        return True

    @abstractmethod
    def sing(self, job: SingJob, voice: Optional[Voice] = None) -> np.ndarray:
        """Return mono float audio in [-1, 1] at job.sample_rate, sung in `voice` (None = default)."""

    def sing_blend(self, job: SingJob, weighted_voices: list) -> np.ndarray:
        """Sing with several voices blended into one new voice. weighted_voices = [(Voice, weight)]."""
        raise NotImplementedError

    def convert(self, audio: np.ndarray, sr: int, f0_hz: np.ndarray, voice: Optional[Voice] = None) -> np.ndarray:
        """Audio-in path: re-sing `audio` (mono, at `sr`) in `voice`, following the pitch curve `f0_hz`
        (one value per 480-sample frame at 24 kHz, 0 = no pitch). Returns mono audio at `sr`."""
        raise NotImplementedError(f"backend '{self.name}' cannot convert audio yet")
