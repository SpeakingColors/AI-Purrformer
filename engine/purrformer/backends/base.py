"""Backend interface. Every voice engine (SoulX-Singer, Seed-VC, ...) implements this."""
from abc import ABC, abstractmethod

import numpy as np

from ..job import SingJob


class Backend(ABC):
    name: str = "base"

    def available(self) -> bool:
        """True if the engine can run here (deps installed, GPU found, ...)."""
        return True

    @abstractmethod
    def sing(self, job: SingJob) -> np.ndarray:
        """Return mono float audio in [-1, 1] at job.sample_rate."""
