"""SoulX-Singer backend (real singing voices). AGPL-3.0 for this file; SoulX-Singer has its own licence.

UNTESTED ON REAL WEIGHTS: written from reading SoulX-Singer's cli/inference.py. Needs a GPU test.
Code is vendored in engine/third_party and the weights ship in the installer (see models.py), so it works offline.
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
from .. import models
from ..voicebank import Voice, VoiceError, default_root
from .base import Backend
from .soulx_meta import job_to_segments

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

    def available(self) -> bool:
        code = models.code_dir()
        if code is None or models.weights_path() is None:
            return False
        try:
            import importlib.util
            if str(code) not in sys.path:
                sys.path.insert(0, str(code))
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
        code = models.code_dir()
        cfg = load_config(str(code / models.CONFIG_REL))
        self._device = "cuda" if torch.cuda.is_available() else "cpu"  # ROCm also reports "cuda"
        model = SoulXSinger(cfg).to(self._device)
        ckpt = torch.load(str(models.weights_path()), weights_only=False, map_location="cpu")
        model.load_state_dict(ckpt["state_dict"], strict=True)
        model.eval().to(self._device)
        self._proc = DataProcessor(hop_size=cfg.audio.hop_size, sample_rate=cfg.audio.sample_rate,
                                   phoneset_path=str(code / "soulxsinger/utils/phoneme/phone_set.json"), device=self._device)
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


# ------------------------------------------------------------------ audio in (convert)
def _convert(self, audio: np.ndarray, sr: int, f0_hz: np.ndarray, voice: Optional[Voice] = None) -> np.ndarray:
    """UNTESTED ON REAL WEIGHTS. Uses SoulX-Singer's SVC model: the recording keeps its words, the
    prompt clip gives the timbre, `f0_hz` gives the pitch. Needs the SVC weights next to model.pt
    (`model-svc.pt`, name to be confirmed on the first GPU test) and openai/whisper-base bundled."""
    if voice is None or voice.kind == "builtin":
        raise VoiceError("the soulx backend needs a real voice (import a clip in the Voices page)")
    svc_ckpt = models.svc_weights_path()
    if svc_ckpt is None or models.code_dir() is None:
        raise VoiceError("the voice conversion model is not installed (see docs/ENGINES.md)")
    import torch
    from soulxsinger.models.soulxsinger_svc import SoulXSingerSVC
    from soulxsinger.utils.file_utils import load_config
    from .. import convert as conv
    code = models.code_dir()
    if str(code) not in sys.path:
        sys.path.insert(0, str(code))
    if getattr(self, "_svc", None) is None:
        cfg = load_config(str(code / "soulxsinger/config/soulxsinger.yaml"))
        dev = "cuda" if torch.cuda.is_available() else "cpu"
        m = SoulXSingerSVC(cfg).to(dev)
        ck = torch.load(str(svc_ckpt), weights_only=False, map_location="cpu")
        m.load_state_dict(ck["state_dict"] if "state_dict" in ck else ck, strict=False)
        self._svc, self._svc_cfg, self._svc_dev = m.eval().to(dev), cfg, dev
    dev, cfg = self._svc_dev, self._svc_cfg
    clip = default_root() / voice.id / voice.clips[0]["file"]
    import soundfile as sf
    pt, psr = sf.read(str(clip), dtype="float32", always_2d=False)
    if getattr(pt, "ndim", 1) == 2:
        pt = pt.mean(axis=1)
    pt = _resample(pt.astype(np.float64), psr, cfg.audio.sample_rate)
    from ..pitch import track_f0
    pt_f0 = track_f0(pt, cfg.audio.sample_rate, hop=cfg.audio.hop_size)
    t = lambda a: torch.from_numpy(np.asarray(a, dtype=np.float32)).unsqueeze(0).to(dev)
    with torch.no_grad():
        gen, _ = self._svc.infer(t(pt), t(_resample(audio, sr, cfg.audio.sample_rate)), t(pt_f0), t(f0_hz),
                                 auto_shift=True, pitch_shift=0, n_steps=cfg.infer.n_steps, cfg=cfg.infer.cfg,
                                 use_fp16=os.environ.get("PURRFORMER_FP16") == "1")
    return _resample(gen.squeeze().float().cpu().numpy(), cfg.audio.sample_rate, sr)


SoulXBackend.convert = _convert
