"""Voice bank: where voices live, and the rules for adding them. AGPL-3.0.

A voice is a name, a rights statement and (for user voices) one or more reference clips.
The engine ships NO third-party voices. Built-in voices are synthetic test timbres.
Layout on disk:  <root>/<voice-id>/voice.json  +  clip-01.wav, clip-02.wav ...
"""
import hashlib
import io
import json
import os
import re
import shutil
import time
import wave
from dataclasses import asdict, dataclass, field
from pathlib import Path

RIGHTS = ("own-voice", "permission", "licensed", "cc0", "cc-by", "public-domain")
MIN_CLIP_S = 1.0
MAX_CLIP_S = 60.0
MAX_CLIP_BYTES = 50 * 1024 * 1024
MAX_CLIPS_PER_VOICE = 8
_ID_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")


class VoiceError(ValueError):
    """A voice problem the caller can fix (bad input, unknown id, not allowed)."""


@dataclass
class Voice:
    id: str
    name: str
    kind: str  # "builtin" or "user"
    rights: str  # one of RIGHTS, or "synthetic" for built-ins
    attribution: str = ""
    notes: str = ""
    clips: list = field(default_factory=list)  # [{"file","duration_s","sample_rate"}]
    params: dict = field(default_factory=dict)  # backend hints, e.g. {"timbre": "saw"}
    created: str = ""

    def public(self) -> dict:
        d = asdict(self)
        d["clips"] = [{"duration_s": c["duration_s"], "sample_rate": c["sample_rate"]} for c in self.clips]
        return d


BUILTIN_VOICES = [
    Voice(id="test-sine", name="Test sine", kind="builtin", rights="synthetic",
          notes="Synthetic test timbre. For checking the plumbing, not for singing.", params={"timbre": "sine"}),
    Voice(id="test-saw", name="Test saw", kind="builtin", rights="synthetic",
          notes="Synthetic test timbre.", params={"timbre": "saw"}),
    Voice(id="test-square", name="Test square", kind="builtin", rights="synthetic",
          notes="Synthetic test timbre.", params={"timbre": "square"}),
    Voice(id="test-hollow", name="Hollow", kind="builtin", rights="synthetic",
          notes="Synthetic test timbre, soft and woody.", params={"timbre": "hollow"}),
    Voice(id="test-warm", name="Warm", kind="builtin", rights="synthetic",
          notes="Synthetic test timbre, mellow.", params={"timbre": "warm"}),
    Voice(id="test-bright", name="Bright", kind="builtin", rights="synthetic",
          notes="Synthetic test timbre, buzzy.", params={"timbre": "bright"}),
    Voice(id="test-choir", name="Choir-ish", kind="builtin", rights="synthetic",
          notes="Synthetic test timbre, vowel-like.", params={"timbre": "choir"}),
    Voice(id="test-organ", name="Organ", kind="builtin", rights="synthetic",
          notes="Synthetic test timbre.", params={"timbre": "organ"}),
]


def default_root() -> Path:
    env = os.environ.get("PURRFORMER_VOICES")
    if env:
        return Path(env)
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / "purrformer" / "voices"


def slugify(name: str) -> str:
    s = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")[:40].strip("-")
    return s or "voice"


def _inspect_wav(data: bytes) -> dict:
    if len(data) > MAX_CLIP_BYTES:
        raise VoiceError("clip is too large (max 50 MB)")
    try:
        with wave.open(io.BytesIO(data), "rb") as w:
            frames, rate = w.getnframes(), w.getframerate()
            if rate <= 0:
                raise VoiceError("clip has no sample rate")
            dur = frames / rate
    except (wave.Error, EOFError) as exc:
        raise VoiceError(f"not a readable PCM WAV file: {exc}")
    if dur < MIN_CLIP_S:
        raise VoiceError(f"clip is too short (min {MIN_CLIP_S:g} s)")
    if dur > MAX_CLIP_S:
        raise VoiceError(f"clip is too long (max {MAX_CLIP_S:g} s)")
    return {"duration_s": round(dur, 3), "sample_rate": rate}


class VoiceBank:
    def __init__(self, root: Path):
        self.root = Path(root)

    # ------------------------------------------------------------ reading
    def _check_id(self, voice_id: str) -> None:
        if not isinstance(voice_id, str) or not _ID_RE.match(voice_id):
            raise VoiceError(f"invalid voice id '{voice_id}'")

    def _user_voices(self) -> list[Voice]:
        out = []
        if self.root.is_dir():
            for d in sorted(self.root.iterdir()):
                f = d / "voice.json"
                if d.is_dir() and _ID_RE.match(d.name) and f.is_file():
                    try:
                        out.append(Voice(**json.loads(f.read_text())))
                    except (ValueError, TypeError):
                        continue  # skip a damaged entry, do not crash the list
        return out

    def all_voices(self) -> list[Voice]:
        return list(BUILTIN_VOICES) + self._user_voices()

    def get(self, voice_id: str) -> Voice:
        self._check_id(voice_id)
        for v in self.all_voices():
            if v.id == voice_id:
                return v
        raise VoiceError(f"unknown voice '{voice_id}'")

    def clip_paths(self, voice_id: str) -> list[Path]:
        v = self.get(voice_id)
        return [self.root / v.id / c["file"] for c in v.clips]

    # ------------------------------------------------------------ writing
    def import_voice(self, name: str, wav_bytes: bytes, rights: str, rights_confirmed: bool,
                     attribution: str = "", notes: str = "") -> Voice:
        name = (name or "").strip()
        if not 1 <= len(name) <= 60:
            raise VoiceError("name must be 1 to 60 characters")
        if rights not in RIGHTS:
            raise VoiceError(f"rights must be one of: {', '.join(RIGHTS)}")
        if not rights_confirmed:
            raise VoiceError("you must confirm you have the rights, or the permission of the person whose voice it is")
        if rights == "cc-by" and not attribution.strip():
            raise VoiceError("CC BY voices need an attribution line")
        info = _inspect_wav(wav_bytes)
        voice_id = f"{slugify(name)}-{hashlib.sha1(wav_bytes).hexdigest()[:6]}"
        existing = [v for v in self._user_voices() if v.id == voice_id]
        if existing:
            return existing[0]
        d = self.root / voice_id
        d.mkdir(parents=True, exist_ok=True)
        (d / "clip-01.wav").write_bytes(wav_bytes)
        voice = Voice(id=voice_id, name=name, kind="user", rights=rights, attribution=attribution.strip(),
                      notes=notes.strip(), clips=[{"file": "clip-01.wav", **info}],
                      created=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()))
        (d / "voice.json").write_text(json.dumps(asdict(voice), indent=2))
        return voice

    def add_clip(self, voice_id: str, wav_bytes: bytes) -> Voice:
        v = self.get(voice_id)
        if v.kind != "user":
            raise VoiceError("built-in voices cannot be changed")
        if len(v.clips) >= MAX_CLIPS_PER_VOICE:
            raise VoiceError(f"a voice can have at most {MAX_CLIPS_PER_VOICE} clips")
        info = _inspect_wav(wav_bytes)
        fname = f"clip-{len(v.clips) + 1:02d}.wav"
        d = self.root / v.id
        (d / fname).write_bytes(wav_bytes)
        v.clips.append({"file": fname, **info})
        (d / "voice.json").write_text(json.dumps(asdict(v), indent=2))
        return v

    def delete(self, voice_id: str) -> None:
        v = self.get(voice_id)
        if v.kind != "user":
            raise VoiceError("built-in voices cannot be deleted")
        d = (self.root / v.id).resolve()
        if self.root.resolve() not in d.parents:
            raise VoiceError("refusing to delete outside the voice folder")
        shutil.rmtree(d)
