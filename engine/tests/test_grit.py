import numpy as np

from purrformer.grit import GritSettings, grit, shaper

SR = 24000
X = 0.3 * np.sin(2 * np.pi * 220 * np.arange(SR) / SR)


def rms(a):
    return float(np.sqrt(np.mean(np.square(a))))


def test_off_and_zero_mix_are_clean():
    assert np.array_equal(grit(X, GritSettings(drive=0.9), SR, "after"), X)  # not enabled
    g = GritSettings(enabled=True, drive=0.9, mix=0.0)
    assert np.allclose(grit(X, g, SR, "after"), X)


def test_positions():
    g = GritSettings(enabled=True, fuzz=0.7, mix=1.0, position="before")
    assert np.array_equal(grit(X, g, SR, "after"), X) and not np.array_equal(grit(X, g, SR, "before"), X)
    g.position = "both"
    assert not np.array_equal(grit(X, g, SR, "before"), X) and not np.array_equal(grit(X, g, SR, "after"), X)
    assert np.array_equal(grit(X, g, SR, "between"), X)
    g.position = "all"
    assert all(not np.array_equal(grit(X, g, SR, w), X) for w in ("before", "between", "after"))


def test_level_matched_and_adds_harmonics():
    for kw in ({"drive": 0.6}, {"transistor": 0.6}, {"fuzz": 0.6}):
        g = GritSettings(enabled=True, mix=1.0, position="after", **kw)
        y = grit(X, g, SR, "after")
        assert 0.7 < rms(y) / rms(X) < 1.3
        spec = np.abs(np.fft.rfft(y))
        assert spec[660] > 0.01 * spec[220] or spec[440] > 0.01 * spec[220]  # 2nd or 3rd harmonic (1 s => bin = Hz)
    assert abs(np.mean(grit(X, GritSettings(enabled=True, transistor=0.8, mix=1.0), SR, "after"))) < 0.01  # no DC


def test_shaper_is_bounded_and_even_with_asymmetry():
    x = np.linspace(-1, 1, 1001)
    for d, t, f in ((1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 1, 1)):
        assert np.max(np.abs(shaper(x, d, t, f))) < 3.0
    y = shaper(x, 0, 0.7, 0)
    assert abs(y.max()) != abs(y.min())  # transistor is lopsided
