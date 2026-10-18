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
