"""Voice packs: install a folder or zip of clips into the voice bank. AGPL-3.0.

    python -m purrformer.voicepack install  my-pack.zip      (has pack.json, see below)
    python -m purrformer.voicepack vocalset VocalSet.zip     (VocalSet from Zenodo, CC BY 4.0)

pack.json: {"name": "...", "rights": "cc-by", "attribution": "...", "voices": [{"name": "...", "files": ["a.wav"], "vowel": "a"}]}
`vowel` (optional) means the clips are one sustained vowel; we then also write the SoulX prompt.json for them.
Nothing is downloaded by this file: you download the pack yourself, so you see its licence page.
"""
import io
import json
import re
import sys
import wave
import zipfile
from pathlib import Path
from typing import Optional

import numpy as np

from .voicebank import MAX_CLIP_S, MIN_CLIP_S, RIGHTS, VoiceBank, VoiceError, default_root

VOWELS = {"a": "en_AA1", "e": "en_EH1", "i": "en_IY1", "o": "en_OW1", "u": "en_UW1"}
VOCALSET_ATTRIBUTION = ("VocalSet: a singing voice dataset, Julia Wilkins, Prem Seetharaman, Alison Wahl, "
                        "Bryan Pardo (ISMIR 2018), CC BY 4.0")


def read_wav(data: bytes):
    """-> (mono float array, sample rate). PCM 16/24/32 bit only."""
    try:
        with wave.open(io.BytesIO(data), "rb") as w:
            ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
            raw = w.readframes(n)
    except (wave.Error, EOFError) as exc:
        raise VoiceError(f"not a readable PCM WAV file: {exc}")
    if sw == 2:
        x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    elif sw == 4:
        x = np.frombuffer(raw, "<i4").astype(np.float64) / 2147483648.0
    elif sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        x = np.where(v >= 1 << 23, v - (1 << 24), v).astype(np.float64) / 8388608.0
    else:
        raise VoiceError("unsupported WAV bit depth")
    if ch > 1:
        x = x[: len(x) // ch * ch].reshape(-1, ch).mean(axis=1)
    return x, sr


def estimate_midi(x: np.ndarray, sr: int) -> Optional[int]:
    """Median pitch of a sustained sung note, as a MIDI number (autocorrelation). None if unvoiced."""
    lo, hi = int(sr / 1000), int(sr / 70)  # 70 Hz to 1000 Hz
    win = int(0.06 * sr)
    if len(x) < win * 2:
        return None
    ests = []
    for s in range(0, len(x) - win, win // 2):
        f = x[s:s + win] - np.mean(x[s:s + win])
        if np.sqrt(np.mean(f * f)) < 0.01:
            continue
        ac = np.correlate(f, f, "full")[win - 1:]
        if ac[0] <= 0:
            continue
        seg = ac[lo:hi]
        if len(seg) == 0:
            continue
        k = int(np.argmax(seg)) + lo
        if ac[k] / ac[0] > 0.6:
            ests.append(sr / k)
    if len(ests) < 3:
        return None
    return int(round(69 + 12 * np.log2(float(np.median(ests)) / 440.0)))


def vowel_prompt(wav_bytes: bytes, vowel: str) -> dict:
    """SoulX prompt metadata for a clip that is one sustained vowel on one pitch."""
    if vowel not in VOWELS:
        raise VoiceError(f"vowel must be one of {', '.join(VOWELS)}")
    x, sr = read_wav(wav_bytes)
    midi = estimate_midi(x, sr)
    if midi is None:
        raise VoiceError("could not find a steady pitch in this clip")
    dur = len(x) / sr
    lead, tail = 0.1, 0.1
    body = max(0.2, dur - lead - tail)
    return {"index": "purr_prompt", "language": "English", "time": [0, int(round(dur * 1000))],
            "duration": f"{lead:.2f} {body:.2f} {tail:.2f}", "text": f"<SP> {vowel}h <SP>",
            "phoneme": f"<SP> {VOWELS[vowel]} <SP>", "note_pitch": f"0 {midi} 0", "note_type": "1 2 1", "f0": ""}


def _install_voice(bank: VoiceBank, name, clips, rights, attribution, vowel=None, notes=""):
    voice = None
    for c in clips:
        if voice is None:
            known = {v.id for v in bank.all_voices()}
            voice = bank.import_voice(name, c, rights, True, attribution, notes)
            if voice.id in known:  # already installed (same first clip): do nothing, so reruns are safe
                return voice
        elif len(voice.clips) < 8:
            try:
                voice = bank.add_clip(voice.id, c)
            except VoiceError:
                pass
    if voice and vowel:
        meta = vowel_prompt(clips[0], vowel)
        (bank.root / voice.id / "prompt.json").write_text(json.dumps([meta], indent=2))
    return voice


def _source(path: Path):
    """-> function(name) -> bytes, and the list of names. Works for a folder or a zip."""
    if path.is_dir():
        names = [str(p.relative_to(path)).replace("\\", "/") for p in path.rglob("*") if p.is_file()]
        return (lambda n: (path / n).read_bytes()), names
    z = zipfile.ZipFile(path)

    def read(n):
        try:
            return z.read(n)
        except zipfile.BadZipFile:  # VocalSet's zip trips Python's reader; other unzip tools cope
            import shutil
            import subprocess
            for cmd in (["unzip", "-p", str(path), n], ["tar", "-xOf", str(path), n]):
                if shutil.which(cmd[0]):
                    r = subprocess.run(cmd, capture_output=True)
                    if r.stdout:
                        return r.stdout
            raise

    return read, [n for n in z.namelist() if not n.endswith("/")]


def install_pack(path: Path, bank: VoiceBank) -> list:
    read, names = _source(Path(path))
    if "pack.json" not in names:
        raise VoiceError("no pack.json in this pack")
    pack = json.loads(read("pack.json"))
    rights = pack.get("rights")
    if rights not in RIGHTS:
        raise VoiceError(f"pack.json rights must be one of: {', '.join(RIGHTS)}")
    out = []
    for v in pack.get("voices", []):
        files = [f for f in v.get("files", []) if f in names]
        if not files:
            continue
        out.append(_install_voice(bank, v["name"], [read(f) for f in files], rights,
                                  pack.get("attribution", ""), v.get("vowel"), pack.get("notes", "")))
    return out


# Real VocalSet names (seen in the zip, 2026-10-18): data_by_vowel/a/f1_long_straight_a.wav
_CLIP = re.compile(r"(?:^|/)([fm]\d+)_(.+)_([aeiou])\.wav$", re.I)
_PREFER = ("straight", "forte", "pp", "messa", "vibrato")  # steadier long tones first


def vocalset_clips(names, vowel: str = "a") -> dict:
    """singer -> long-tone clip names of that vowel, steadiest technique first. Skips macOS junk."""
    out: dict = {}
    for n in names:
        if "__MACOSX" in n or "/._" in n:
            continue
        m = _CLIP.search(n)
        if m and m.group(3).lower() == vowel and m.group(2).lower().startswith("long"):
            out.setdefault(m.group(1).lower(), []).append(n)

    def rank(n):
        t = _CLIP.search(n).group(2).lower()
        return next((i for i, k in enumerate(_PREFER) if k in t), len(_PREFER)), n

    return {k: sorted(v, key=rank) for k, v in sorted(out.items())}


def install_vocalset(path: Path, bank: VoiceBank, vowel: str = "a", per_voice: int = 2) -> list:
    """One voice per singer, built from that singer's sustained '<vowel>' long-tone clips."""
    read, names = _source(Path(path))
    out = []
    for s, files in vocalset_clips(names, vowel).items():
        good = []
        for f in files:
            try:
                data = read(f)
                x, sr = read_wav(data)
            except (VoiceError, OSError, zipfile.BadZipFile):
                continue
            if MIN_CLIP_S <= len(x) / sr <= MAX_CLIP_S and estimate_midi(x, sr) is not None:
                good.append(data)
            if len(good) >= per_voice:
                break
        if good:
            out.append(_install_voice(bank, f"VocalSet {s}", good, "cc-by", VOCALSET_ATTRIBUTION, vowel,
                                      "Sustained vowel clips from VocalSet. Good for timbre, not for diction."))
    if not out:
        raise VoiceError("found no usable long-tone clips; is this the VocalSet zip?")
    return out


def build_vocalset_pack(path: Path, outdir: Path, vowel: str = "a", per_voice: int = 2, max_s: float = 8.0, min_voices: int = 10) -> int:
    """Make a small bundle-ready pack (pack.json + short wavs) from the big VocalSet zip. Used by CI."""
    from .wav import to_wav_bytes
    read, names = _source(Path(path))
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    voices = []
    for s, files in vocalset_clips(names, vowel).items():
        made = []
        for f in files:
            try:
                x, sr = read_wav(read(f))
            except (VoiceError, OSError, zipfile.BadZipFile):
                continue
            x = x[: int(max_s * sr)]
            if len(x) / sr >= MIN_CLIP_S and estimate_midi(x, sr) is not None:
                fn = f"{s}-{len(made) + 1}.wav"
                (outdir / fn).write_bytes(to_wav_bytes(x, sr))
                made.append(fn)
            if len(made) >= per_voice:
                break
        if made:
            voices.append({"name": f"VocalSet {s}", "files": made, "vowel": vowel})
    if len(voices) < min_voices:  # VocalSet has 20 singers: far fewer means the reader missed files
        raise VoiceError(f"only {len(voices)} usable singers found (expected about 20); the zip was not read fully")
    (outdir / "pack.json").write_text(json.dumps({
        "name": "VocalSet starter", "rights": "cc-by", "attribution": VOCALSET_ATTRIBUTION,
        "notes": "Sustained vowel clips from VocalSet. Good for timbre, not for diction.", "voices": voices}, indent=2))
    return len(voices)


def install_bundled(bank: VoiceBank, folder: Optional[Path] = None) -> list:
    """Install every pack found in the bundled voices folder. Safe to run on every start."""
    import os
    folder = Path(folder or os.environ.get("PURRFORMER_BUNDLED") or Path(__file__).resolve().parents[1] / "bundled_voices")
    out = []
    if not folder.is_dir():
        return out
    for p in sorted(folder.iterdir()):
        if (p.is_dir() and (p / "pack.json").is_file()) or p.suffix == ".zip":
            try:
                out += install_pack(p, bank)
            except (VoiceError, OSError, ValueError, zipfile.BadZipFile) as exc:
                print(f"bundled pack {p.name} skipped: {exc}")
    return out


def main(argv=None) -> int:
    a = list(sys.argv[1:] if argv is None else argv)
    if len(a) == 3 and a[0] == "build-vocalset":
        print(f"{build_vocalset_pack(Path(a[1]), Path(a[2]))} voices written to {a[2]}")
        return 0
    if len(a) != 2 or a[0] not in ("install", "vocalset"):
        print(__doc__)
        return 2
    bank = VoiceBank(default_root())
    try:
        voices = (install_pack if a[0] == "install" else install_vocalset)(Path(a[1]), bank)
    except (VoiceError, OSError, zipfile.BadZipFile) as exc:
        print(f"error: {exc}")
        return 1
    for v in voices:
        print(f"installed: {v.name} ({v.id}, {len(v.clips)} clips)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
