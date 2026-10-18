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
