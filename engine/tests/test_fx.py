import numpy as np
import pytest

from purrformer.fx import apply_fx, bitcrush, saturate, track_pitch, robot
from purrformer.job import SingJob, Note

SR = 44100


def sine(f, secs, sr=SR):
    t = np.arange(int(secs * sr)) / sr
    return 0.5 * np.sin(2 * np.pi * f * t)


def pitch_of(x, sr=SR):
    _, f0 = track_pitch(x, sr)
    f0 = f0[f0 > 0]
    return float(np.median(f0)) if len(f0) else 0.0


def cents(a, b):
    return 1200 * np.log2(a / b)


def test_tracker_finds_pitch():
    assert pitch_of(sine(220, 0.5)) == pytest.approx(220, rel=0.01)


def test_robot_pulls_off_pitch_voice_onto_midi_note():
    # voice is ~50 cents sharp of A3 (220 Hz); note says A3
    x = sine(220 * 2 ** (50 / 1200), 1.0)
    job = SingJob(tempo_bpm=60, notes=[Note(start_beats=0, length_beats=1, pitch=57)])
    job.fx.robot.enabled = True
    job.fx.robot.mix = 1.0
    y = robot(x, SR, job)
    mid = y[int(0.3 * SR):int(0.8 * SR)]
    assert abs(cents(pitch_of(mid), 220.0)) < 15


def test_robot_mix_zero_is_clean():
    x = sine(230, 0.5)
    job = SingJob(tempo_bpm=60, notes=[Note(start_beats=0, length_beats=1, pitch=57)])
    job.fx.robot.enabled = True
    job.fx.robot.mix = 0.0
    assert np.allclose(apply_fx(x, job), x)


def test_fx_disabled_changes_nothing():
    x = sine(200, 0.3)
    assert np.allclose(apply_fx(x, SingJob()), x)


def test_bitcrush_reduces_levels():
    x = np.sin(np.linspace(0, 40, 4000)) * 0.9
    assert len(np.unique(bitcrush(x, 1.0))) < len(np.unique(x)) / 20
    assert np.array_equal(bitcrush(x, 0.0), x)


def test_saturate_bounded_and_zero_drive_identity():
    x = np.linspace(-1, 1, 1000)
    assert np.array_equal(saturate(x, 0.0), x)
    assert np.max(np.abs(saturate(x, 1.0))) <= 1.001


def test_empty_audio_ok():
    assert len(apply_fx(np.zeros(0), SingJob())) == 0


# ---------------------------------------------------------------- supersaw
def super_job(**kw):
    j = SingJob(tempo_bpm=60, notes=[Note(start_beats=0, length_beats=2, pitch=57)])
    j.fx.supersaw.enabled = True
    for k, v in kw.items():
        setattr(j.fx.supersaw, k, v)
    return j


def test_supersaw_is_stereo_with_different_channels_and_similar_loudness():
    x = sine(220, 1.0)
    y = apply_fx(x, super_job(copies=7, detune=0.6, spread=1.0, widen=0.5))
    assert y.shape == (len(x), 2)
    assert not np.allclose(y[:, 0], y[:, 1], atol=1e-3)
    assert np.max(np.abs(y)) <= 1.0
    rms_in, rms_out = np.sqrt(np.mean(x ** 2)), np.sqrt(np.mean(y ** 2))
    assert rms_out == pytest.approx(rms_in, rel=0.15)


def test_supersaw_keeps_the_pitch_centre():
    x = sine(220, 1.0)
    y = apply_fx(x, super_job(copies=7, detune=0.5))
    mono = y.mean(axis=1)[int(0.2 * SR):int(0.9 * SR)]
    assert abs(cents(pitch_of(mono), 220.0)) < 40


def test_more_detune_spreads_the_spectrum():
    x = sine(220, 1.0)

    def bandwidth(sig):
        s = np.abs(np.fft.rfft(sig * np.hanning(len(sig))))
        f = np.fft.rfftfreq(len(sig), 1 / SR)
        band = (f > 150) & (f < 300)
        p = s[band] ** 2
        c = (p * f[band]).sum() / p.sum()
        return np.sqrt((p * (f[band] - c) ** 2).sum() / p.sum())

    lo = apply_fx(x, super_job(detune=0.05)).mean(axis=1)
    hi = apply_fx(x, super_job(detune=1.0)).mean(axis=1)
    assert bandwidth(hi) > 1.5 * bandwidth(lo)


def test_supersaw_off_stays_mono_and_dirt_works_on_stereo():
    x = sine(220, 0.5)
    assert apply_fx(x, SingJob()).ndim == 1
    j = super_job()
    j.fx.dirt.enabled = True
    j.fx.dirt.drive = 0.5
    j.fx.dirt.bitcrush = 0.3
    y = apply_fx(x, j)
    assert y.shape == (len(x), 2) and np.max(np.abs(y)) <= 1.0


def test_wav_stereo_roundtrip():
    import io, wave
    from purrformer.wav import to_wav_bytes
    st = np.stack([sine(220, 0.1), sine(330, 0.1)], axis=1)
    w = wave.open(io.BytesIO(to_wav_bytes(st, SR)))
    assert w.getnchannels() == 2 and w.getnframes() == len(st)
