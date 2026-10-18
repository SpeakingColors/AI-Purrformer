"""Write 16-bit WAV bytes, mono (n,) or stereo (n, 2). AGPL-3.0."""
import io
import wave

import numpy as np


def to_wav_bytes(audio: np.ndarray, sample_rate: int) -> bytes:
    x = np.clip(np.asarray(audio, dtype=np.float64), -1.0, 1.0)
    channels = 1 if x.ndim == 1 else x.shape[1]
    pcm = (x * 32767.0).astype("<i2")
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(pcm.tobytes())  # interleaved for (n, 2)
    return buf.getvalue()
