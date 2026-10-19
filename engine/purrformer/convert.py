"""Audio-in path: a sung or spoken recording becomes the AI voice, following the song. AGPL-3.0.

Steps: track the pitch of the recording, pull it toward the song's key (and toward MIDI notes if
given), hand the recording plus the new pitch curve to the backend, then apply the FX.
The request body is: 4 bytes (little endian) = JSON length, the JSON, then the WAV bytes.
"""
import json
import struct
from typing import Literal, Optional

import numpy as np
from pydantic import BaseModel, Field

from . import analysis
from .backends.base import Backend
from .fx import apply_fx
from .job import FxSettings, Note, SingJob, VoiceSlot
from .pitch import guide_from_notes, snap_f0, track_f0
from .voicebank import VoiceBank, VoiceError

HOP = 480
WORK_SR = 24000  # SoulX works at 24 kHz


class ConvertJob(BaseModel):
    version: Literal[1] = 1
    backend: str = "tone"
    sample_rate: int = Field(default=44100, ge=8000, le=192000)
    tempo_bpm: float = Field(default=120.0, ge=20, le=400)
    voices: list[VoiceSlot] = Field(default_factory=list)
    fx: FxSettings = FxSettings()
    # key: "auto" = detect from `context` audio (sidechain/master) if sent, else from the recording
    key: str = "auto"  # "auto", "off", or like "A minor" / "C major"
    snap_strength: float = Field(default=0.0, ge=0, le=1)  # 0 = keep how it was sung
    notes: list[Note] = Field(default_factory=list)  # optional MIDI from another track
    midi_strength: float = Field(default=1.0, ge=0, le=1)  # 1 = follow the MIDI exactly
    context_bytes: int = Field(default=0, ge=0)  # length of an optional second WAV after the first

    def normalised_voices(self) -> list[VoiceSlot]:
        total = sum(v.weight for v in self.voices)
        return [VoiceSlot(id=v.id, weight=v.weight / total) for v in self.voices] if total > 0 else []


def pack(job: dict, wav: bytes, context_wav: bytes = b"") -> bytes:
    job = dict(job, context_bytes=len(context_wav))
    j = json.dumps(job).encode()
    return struct.pack("<I", len(j)) + j + wav + context_wav


def unpack(body: bytes):
    if len(body) < 4:
        raise ValueError("body too short")
    (n,) = struct.unpack("<I", body[:4])
    if n <= 0 or 4 + n > len(body):
        raise ValueError("bad job length")
    job = ConvertJob.model_validate_json(body[4:4 + n])
    rest = body[4 + n:]
    split = len(rest) - job.context_bytes
    if split <= 0:
        raise ValueError("no audio in body")
    return job, rest[:split], rest[split:]


def _resample(a: np.ndarray, sr_from: int, sr_to: int) -> np.ndarray:
    if sr_from == sr_to or len(a) == 0:
        return a
    n = int(round(len(a) * sr_to / sr_from))
    return np.interp(np.linspace(0, len(a) - 1, n), np.arange(len(a)), a)


def resolve_scale(job: ConvertJob, recording, sr, context=None) -> Optional[list]:
    if job.key == "off":
        return None
    if job.key != "auto":
        tonic, _, mode = job.key.partition(" ")
        if tonic not in analysis.NOTE_NAMES or mode not in ("major", "minor"):
            raise VoiceError(f"key '{job.key}' not understood, use e.g. 'A minor'")
        t = analysis.NOTE_NAMES.index(tonic)
        steps = analysis.MAJOR_STEPS if mode == "major" else analysis.MINOR_STEPS
        return sorted((t + s) % 12 for s in steps)
    src = context if context is not None else (recording, sr)
    k = analysis.detect_key(*src)
    return k["scale"] or None


def target_f0(job: ConvertJob, audio24: np.ndarray, scale: Optional[list]) -> np.ndarray:
    f0 = track_f0(audio24, WORK_SR, hop=HOP)
    if scale and job.snap_strength > 0:
        f0 = snap_f0(f0, scale, job.snap_strength)
    if job.notes:
        spb = 60.0 / job.tempo_bpm
        notes = [(n.start_beats * spb, (n.start_beats + n.length_beats) * spb, n.pitch) for n in job.notes]
        f0 = guide_from_notes(notes, len(f0), HOP / WORK_SR, job.midi_strength, f0)
    return f0


def convert_job(job: ConvertJob, recording, sr: int, backend: Backend, bank: VoiceBank, context=None):
    """recording = mono float array at `sr`. Returns mono float audio at job.sample_rate."""
    if len(recording) < sr // 10:
        raise VoiceError("the recording is empty or too short")
    a24 = _resample(np.asarray(recording, dtype=np.float64), sr, WORK_SR)
    scale = resolve_scale(job, recording, sr, context)
    f0 = target_f0(job, a24, scale)
    slots = [s for s in job.normalised_voices() if s.weight > 0]
    mix = np.zeros(0)
    for s in (slots or [None]):
        voice = bank.get(s.id) if s else None
        a = backend.convert(a24, WORK_SR, f0, voice)
        a = _resample(a, WORK_SR, job.sample_rate)
        w = s.weight if s else 1.0
        if len(a) > len(mix):
            mix = np.pad(mix, (0, len(a) - len(mix)))
        mix[:len(a)] += w * a
    sing = SingJob(sample_rate=job.sample_rate, tempo_bpm=job.tempo_bpm, fx=job.fx)
    out = apply_fx(mix, sing)
    peak = float(np.max(np.abs(out))) if len(out) else 0.0
    return out / peak * 0.9 if peak > 1 else out
