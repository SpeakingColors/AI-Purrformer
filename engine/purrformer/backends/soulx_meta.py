"""Turn a SingJob into SoulX-Singer "score" metadata. AGPL-3.0.

Pure Python, no torch: it can be tested anywhere. Format taken from SoulX-Singer's
example/audio/*.json. note_type: 1 = rest, 2 = new word, 3 = same word held on a new note.
Phonemes are ARPAbet joined with '-' and prefixed 'en_' (English only for now).
"""
import re
from typing import Callable, Optional

from ..job import SingJob

MIN_NOTE_S = 0.04          # shorter notes are dropped (the model works on ~20 ms frames)
MAX_SEGMENT_S = 12.0       # assumption: examples are 7 to 10 s; unverified upper limit
SPLIT_REST_S = 0.5         # prefer to cut a segment at a rest at least this long
_WORD = re.compile(r"[A-Za-z]+(?:'[A-Za-z]+)*")


def default_g2p() -> Callable[[str], list]:
    """English word -> ARPAbet list, using g2p_en (same tool SoulX uses). Imported lazily."""
    from g2p_en import G2p  # type: ignore
    g = G2p()
    ok = re.compile(r"^[A-Z]+[0-2]?$")
    return lambda w: [p for p in g(w.lower()) if ok.match(p)] or ["AH0"]


def split_words(text: Optional[str]) -> list:
    return _WORD.findall(text or "")


def _words_for_notes(job: SingJob, n: int) -> list:
    """One word per note. Per-note lyrics win; then the lyric text in order; then 'la'.
    Extra notes beyond the words hold the last word (note_type 3)."""
    words = [split_words(note.lyric)[0] if split_words(note.lyric) else None for note in job.notes]
    pool = split_words(job.lyrics)
    out, k, last = [], 0, None
    for w in words[:n]:
        if w is None:
            if k < len(pool):
                w, k = pool[k], k + 1
                out.append((w, False))
            elif last is not None and pool:
                out.append((last, True))  # lyrics ran out: hold the last word
            else:
                out.append(("la", False) if not pool else (last or "la", True))
            last = out[-1][0]
        else:
            out.append((w, False))
            last = w
    return out


def job_to_segments(job: SingJob, g2p: Optional[Callable] = None, max_segment_s: float = MAX_SEGMENT_S) -> list:
    """Return a list of SoulX metadata dicts (one per segment), each with time [start_ms, end_ms]."""
    g2p = g2p or default_g2p()
    notes = []
    for note, (word, hold) in zip(job.notes, _words_for_notes(job, len(job.notes))):
        s = job.beats_to_seconds(note.start_beats)
        e = s + job.beats_to_seconds(note.length_beats)
        notes.append({"s": s, "e": e, "p": max(1, note.pitch), "w": word, "hold": hold})
    # monophonic: trim overlaps, drop slivers
    for a, b in zip(notes, notes[1:]):
        if a["e"] > b["s"]:
            a["e"] = b["s"]
    notes = [n for n in notes if n["e"] - n["s"] >= MIN_NOTE_S]
    if not notes:
        return []

    # group notes into segments, cutting at long rests or when too long
    groups, cur = [], [notes[0]]
    for prev, n in zip(notes, notes[1:]):
        gap = n["s"] - prev["e"]
        too_long = n["e"] - cur[0]["s"] > max_segment_s
        if (gap >= SPLIT_REST_S and (prev["e"] - cur[0]["s"]) > max_segment_s / 2) or too_long:
            groups.append(cur)
            cur = [n]
        else:
            cur.append(n)
    groups.append(cur)

    cache: dict = {}

    def phon(word: str) -> str:
        if word not in cache:
            cache[word] = "en_" + "-".join(g2p(word))
        return cache[word]

    segs = []
    for g in groups:
        t0 = g[0]["s"]
        rows, cursor = [], t0
        for n in g:
            if n["s"] - cursor > 1e-3:
                rows.append(["<SP>", "<SP>", 0, 1, n["s"] - cursor])
            rows.append([n["w"], phon(n["w"]), n["p"], 3 if n["hold"] else 2, n["e"] - n["s"]])
            cursor = n["e"]
        # examples begin and end with a rest; keep a tiny one at each end
        rows.insert(0, ["<SP>", "<SP>", 0, 1, 0.1])
        rows.append(["<SP>", "<SP>", 0, 1, 0.1])
        start = max(0.0, t0 - 0.1)
        total = sum(r[4] for r in rows)
        segs.append({
            "index": f"purr_{int(start * 1000)}_{int((start + total) * 1000)}",
            "language": "English",
            "time": [int(round(start * 1000)), int(round((start + total) * 1000))],
            "duration": " ".join(f"{r[4]:.2f}" for r in rows),
            "text": " ".join(r[0] for r in rows),
            "phoneme": " ".join(r[1] for r in rows),
            "note_pitch": " ".join(str(r[2]) for r in rows),
            "note_type": " ".join(str(r[3]) for r in rows),
            "f0": "",
        })
    return segs
