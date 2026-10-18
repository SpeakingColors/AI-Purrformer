"""Read a standard MIDI file into our Note list. AGPL-3.0."""
import io
from dataclasses import dataclass

import mido

from .job import Note


@dataclass
class ParsedMidi:
    tempo_bpm: float
    notes: list[Note]


def parse_midi(data: bytes) -> ParsedMidi:
    """Parse MIDI bytes. Uses the first tempo found (default 120 BPM).

    Lyric meta events are attached to the first note starting at or after them.
    Channel 10 (drums) is skipped.
    """
    mid = mido.MidiFile(file=io.BytesIO(data))
    tpb = mid.ticks_per_beat
    tempo_us = None
    open_notes: dict[tuple[int, int], tuple[int, int]] = {}  # (ch, pitch) -> (start_tick, vel)
    raw: list[tuple[int, int, int, int]] = []  # start, end, pitch, vel
    lyric_events: list[tuple[int, str]] = []

    for track in mid.tracks:
        tick = 0
        open_notes.clear()
        for msg in track:
            tick += msg.time
            if msg.type == "set_tempo" and tempo_us is None:
                tempo_us = msg.tempo
            elif msg.type == "lyrics":
                text = msg.text.strip()
                if text:
                    lyric_events.append((tick, text))
            elif msg.type == "note_on" and msg.velocity > 0:
                if msg.channel == 9:
                    continue
                open_notes[(msg.channel, msg.note)] = (tick, msg.velocity)
            elif msg.type in ("note_off", "note_on"):  # note_on vel 0 is off
                if msg.channel == 9:
                    continue
                start = open_notes.pop((msg.channel, msg.note), None)
                if start is not None and tick > start[0]:
                    raw.append((start[0], tick, msg.note, start[1]))

    raw.sort(key=lambda r: (r[0], r[2]))
    notes: list[Note] = []
    lyric_events.sort()
    li = 0
    for s, e, p, v in raw:
        lyric = None
        while li < len(lyric_events) and lyric_events[li][0] <= s:
            lyric = lyric_events[li][1]
            li += 1
        notes.append(Note(start_beats=s / tpb, length_beats=(e - s) / tpb,
                          pitch=p, velocity=v, lyric=lyric))
    bpm = 60_000_000 / tempo_us if tempo_us else 120.0
    return ParsedMidi(tempo_bpm=bpm, notes=notes)
