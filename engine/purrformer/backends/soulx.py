"""SoulX-Singer backend (real singing voices). AGPL-3.0 for this file; SoulX-Singer has its own licence.

UNTESTED ON REAL WEIGHTS: written from reading SoulX-Singer's cli/inference.py. Needs a GPU test.
Setup: clone SoulX-Singer, download its weights, set PURRFORMER_SOULX_DIR to the clone folder.
A voice needs: a clip (wav) AND `prompt.json` next to it, the SoulX metadata that describes the clip
(lyrics, notes). SoulX's preprocess tools make that file (see docs/VOICE-SOURCES.md).
"""
import json
import os
import sys
from pathlib import Path
from typing import Optional

import numpy as np

from ..job import SingJob
from ..voicebank import Voice, VoiceError, default_root
from .base import Backend
from .soulx_meta import job_to_segments

MODEL_REL = Path("pretrained_models/SoulX-Singer/model.pt")
CONFIG_REL = Path("soulxsinger/config/soulxsinger.yaml")
PHONESET_REL = Path("soulxsinger/utils/phoneme/phone_set.json")


def _resample(a: np.ndarray, sr_from: int, sr_to: int) -> np.ndarray:
    if sr_from == sr_to or len(a) == 0:
        return a
    n = int(round(len(a) * sr_to / sr_from))
    return np.interp(np.linspace(0, len(a) - 1, n), np.arange(len(a)), a)


class SoulXBackend(Backend):
    name = "soulx"
    blend_modes = ("layer",)

    def __init__(self):
        self._model = None
        self._cfg = None
        self._proc = None

    @staticmethod
    def root() -> Optional[Path]:
        v = os.environ.get("PURRFORMER_SOULX_DIR")
        if v:
            return Path(v)
        from ..models import soulx_dir  # the folder the first-run download fills
        return soulx_dir()

    def available(self) -> bool:
        r = self.root()
        if not r or not (r / MODEL_REL).is_file():
            return False
        try:
            import importlib.util
            if str(r) not in sys.path:
                sys.path.insert(0, str(r))
            return all(importlib.util.find_spec(m) for m in ("torch", "soundfile", "soulxsinger", "g2p_en"))
        except Exception:
            return False

    # ---------------------------------------------------------------- model
    def _load(self):
        if self._model is not None:
            return
        import torch
        from soulxsinger.models.soulxsinger import SoulXSinger
        from soulxsinger.utils.data_processor import DataProcessor
        from soulxsinger.utils.file_utils import load_config
        r = self.root()
        cfg = load_config(str(r / CONFIG_REL))
        self._device = "cuda" if torch.cuda.is_available() else "cpu"  # ROCm also reports "cuda"
        model = SoulXSinger(cfg).to(self._device)
        ckpt = torch.load(str(r / MODEL_REL), weights_only=False, map_location="cpu")
        model.load_state_dict(ckpt["state_dict"], strict=True)
        model.eval().to(self._device)
        self._proc = DataProcessor(hop_size=cfg.audio.hop_size, sample_rate=cfg.audio.sample_rate,
                                   phoneset_path=str(r / PHONESET_REL), device=self._device)
        self._model, self._cfg = model, cfg

    def _prompt_for(self, voice: Voice):
        vdir = default_root() / voice.id
        clips = [vdir / c["file"] for c in voice.clips]
        meta_file = vdir / "prompt.json"
        if not clips:
            raise VoiceError(f"voice '{voice.id}' has no clip")
        if not meta_file.is_file():
            raise VoiceError(f"voice '{voice.id}' has no prompt.json (the lyrics and notes of its clip). "
                             "Make it with SoulX-Singer's preprocess tools, see docs/VOICE-SOURCES.md")
        data = json.loads(meta_file.read_text(encoding="utf-8"))
        meta = data[0] if isinstance(data, list) else data
        return clips[0], meta

    # ---------------------------------------------------------------- sing
    def sing(self, job: SingJob, voice: Optional[Voice] = None) -> np.ndarray:
        if voice is None or voice.kind == "builtin":
            raise VoiceError("the soulx backend needs a real voice (import a clip in the Voices page)")
        if not self.available():
            raise VoiceError("soulx backend is not set up (see docs/ENGINES.md)")
        import torch
        segs = job_to_segments(job)
        out_sr = job.sample_rate
        total = int((job.duration_seconds() + 1.0) * out_sr)
        out = np.zeros(total, dtype=np.float64)
        if not segs:
            return out
        self._load()
        wav, pmeta = self._prompt_for(voice)
        prompt = self._proc.process(pmeta, str(wav))
        fp16 = os.environ.get("PURRFORMER_FP16") == "1"
        for seg in segs:
            target = self._proc.process(dict(seg), None)
            with torch.no_grad():
                gen = self._model.infer({"prompt": prompt, "target": target}, auto_shift=True, pitch_shift=0,
                                        n_steps=self._cfg.infer.n_steps, cfg=self._cfg.infer.cfg,
                                        control="score", use_fp16=fp16)
            a = _resample(gen.squeeze().float().cpu().numpy(), self._cfg.audio.sample_rate, out_sr)
            i = int(seg["time"][0] / 1000 * out_sr)
            n = max(0, min(len(a), total - i))
            out[i:i + n] += a[:n]
        peak = np.max(np.abs(out)) if len(out) else 0
        return out / peak * 0.9 if peak > 1 else out
