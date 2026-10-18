"""Write mono 16-bit WAV bytes. AGPL-3.0."""
import io
import wave

import numpy as np


def to_wav_bytes(audio: np.ndarray, sample_rate: int) -> bytes:
    x = np.clip(np.asarray(audio, dtype=np.float64), -1.0, 1.0)
    pcm = (x * 32767.0).astype("<i2")
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(pcm.tobytes())
    return buf.getvalue()
