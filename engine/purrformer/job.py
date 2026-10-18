"""Job format between the plugin and the engine. AGPL-3.0.

This is OUR format. Adapters turn it into whatever each AI backend needs.
Times are in beats so the job does not depend on sample rate.
"""
from typing import Literal, Optional

from pydantic import BaseModel, Field, model_validator

MAX_NOTES = 5000


class Note(BaseModel):
    start_beats: float = Field(ge=0)
    length_beats: float = Field(gt=0)
    pitch: int = Field(ge=0, le=127)
    velocity: int = Field(default=100, ge=1, le=127)
    lyric: Optional[str] = None


class VoiceSlot(BaseModel):
    """One voice in the blend. Weights are normalised by the engine."""
    id: str
    weight: float = Field(default=1.0, ge=0)


class RobotFx(BaseModel):
    enabled: bool = False
    retune_speed: float = Field(default=1.0, ge=0, le=1)  # 1 = instant (robot)
    mix: float = Field(default=0.5, ge=0, le=1)  # 0 = clean AI voice, 1 = all robot


class DirtFx(BaseModel):
    enabled: bool = False
    drive: float = Field(default=0.0, ge=0, le=1)  # saturation / tube
    bitcrush: float = Field(default=0.0, ge=0, le=1)
    artifacts: float = Field(default=0.0, ge=0, le=1)  # on-purpose glitches
    mix: float = Field(default=1.0, ge=0, le=1)


class FxSettings(BaseModel):
    robot: RobotFx = RobotFx()
    dirt: DirtFx = DirtFx()


class SingJob(BaseModel):
    version: Literal[1] = 1
    mode: Literal["melody_leads", "voice_leads"] = "melody_leads"
    backend: str = "tone"
    sample_rate: int = Field(default=44100, ge=8000, le=192000)
    tempo_bpm: float = Field(default=120.0, ge=20, le=400)
    notes: list[Note] = Field(default_factory=list)
    lyrics: Optional[str] = None
    voices: list[VoiceSlot] = Field(default_factory=list)
    # "layer": render every voice separately and mix the audio (always works).
    # "embedding": blend the voices inside the engine to make one new voice (backend must support it).
    blend_mode: Literal["layer", "embedding"] = "layer"
    fx: FxSettings = FxSettings()

    @model_validator(mode="after")
    def _check(self):
        if len(self.notes) > MAX_NOTES:
            raise ValueError(f"too many notes (max {MAX_NOTES})")
        if self.voices and sum(v.weight for v in self.voices) <= 0:
            raise ValueError("voice weights must not all be zero")
        self.notes = sorted(self.notes, key=lambda n: (n.start_beats, n.pitch))
        return self

    def normalised_voices(self) -> list[VoiceSlot]:
        total = sum(v.weight for v in self.voices)
        if total <= 0:
            return []
        return [VoiceSlot(id=v.id, weight=v.weight / total) for v in self.voices]

    def beats_to_seconds(self, beats: float) -> float:
        return beats * 60.0 / self.tempo_bpm

    def duration_seconds(self) -> float:
        if not self.notes:
            return 0.0
        end = max(n.start_beats + n.length_beats for n in self.notes)
        return self.beats_to_seconds(end)
