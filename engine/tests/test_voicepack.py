import json
import zipfile

import numpy as np

from purrformer import voicepack
from purrformer.voicebank import VoiceBank
from purrformer.wav import to_wav_bytes


def tone(freq, secs=3.0, sr=22050):
    t = np.arange(int(secs * sr)) / sr
    return to_wav_bytes(0.5 * np.sin(2 * np.pi * freq * t) + 0.2 * np.sin(2 * np.pi * 2 * freq * t), sr)


def test_pitch_estimate():
    x, sr = voicepack.read_wav(tone(220.0))
    assert voicepack.estimate_midi(x, sr) == 57
    x, sr = voicepack.read_wav(tone(329.63))
    assert voicepack.estimate_midi(x, sr) == 64


def test_noise_has_no_pitch():
    rng = np.random.default_rng(1)
    d = to_wav_bytes(rng.uniform(-0.3, 0.3, 66000), 22050)
    x, sr = voicepack.read_wav(d)
    assert voicepack.estimate_midi(x, sr) is None


def test_vowel_prompt_shape():
    m = voicepack.vowel_prompt(tone(220.0), "a")
    assert m["phoneme"] == "<SP> en_AA1 <SP>" and m["note_pitch"] == "0 57 0"
    assert abs(sum(map(float, m["duration"].split())) - 3.0) < 0.05


def test_install_pack_folder(tmp_path):
    pk = tmp_path / "pack"
    pk.mkdir()
    (pk / "a.wav").write_bytes(tone(220.0))
    (pk / "pack.json").write_text(json.dumps({"name": "x", "rights": "cc0", "voices": [
        {"name": "Voice A", "files": ["a.wav"], "vowel": "a"}]}))
    bank = VoiceBank(tmp_path / "bank")
    (v,) = voicepack.install_pack(pk, bank)
    assert v.rights == "cc0" and (bank.root / v.id / "prompt.json").is_file()


def test_vocalset_layout(tmp_path):
    z = tmp_path / "vs.zip"
    with zipfile.ZipFile(z, "w") as zf:
        zf.writestr("data_by_vowel/a/f1_long_straight_a.wav", tone(262.0))
        zf.writestr("data_by_vowel/a/m2_long_straight_a.wav", tone(130.8))
        zf.writestr("data_by_vowel/e/m2_long_straight_e.wav", tone(130.8))
    bank = VoiceBank(tmp_path / "bank")
    vs = voicepack.install_vocalset(z, bank)
    assert sorted(v.name for v in vs) == ["VocalSet f1", "VocalSet m2"]
    assert all(v.rights == "cc-by" and "CC BY 4.0" in v.attribution for v in vs)
    assert len(vs[0].clips) == 1


def test_bad_pack(tmp_path):
    import pytest
    from purrformer.voicebank import VoiceError
    (tmp_path / "e").mkdir()
    with pytest.raises(VoiceError):
        voicepack.install_pack(tmp_path / "e", VoiceBank(tmp_path / "b"))


def test_build_then_bundled_install_is_idempotent(tmp_path):
    z = tmp_path / "vs.zip"
    with zipfile.ZipFile(z, "w") as zf:
        zf.writestr("x/data_by_vowel/a/f1_long_straight_a.wav", tone(262.0, 10.0))
        zf.writestr("x/data_by_vowel/a/f1_long_forte_a.wav", tone(262.0, 4.0))
    bundled = tmp_path / "engine" / "bundled_voices" / "vocalset"
    assert voicepack.build_vocalset_pack(z, bundled) == 1
    bank = VoiceBank(tmp_path / "bank")
    first = voicepack.install_bundled(bank, tmp_path / "engine" / "bundled_voices")
    second = voicepack.install_bundled(bank, tmp_path / "engine" / "bundled_voices")
    assert len(first) == 1 and first[0].id == second[0].id
    assert len(bank._user_voices()) == 1 and len(bank._user_voices()[0].clips) == 2
    assert (bank.root / first[0].id / "prompt.json").is_file()


def test_no_bundled_folder_is_fine(tmp_path):
    assert voicepack.install_bundled(VoiceBank(tmp_path / "b"), tmp_path / "nope") == []


def test_real_vocalset_names():
    names = ["data_by_vowel/u/f1_scales_c_fast_piano_u.wav", "data_by_vowel/a/m8_long_trillo_a.wav",
             "data_by_vowel/a/m8_long_straight_a.wav", "data_by_vowel/a/f9_scales_belt_a.wav",
             "__MACOSX/data_by_vowel/a/._m8_long_straight_a.wav", "data_by_vowel/a/f2_long_forte_e.wav",
             "readme-anon.txt"]
    got = voicepack.vocalset_clips(names, "a")
    assert got == {"m8": ["data_by_vowel/a/m8_long_straight_a.wav", "data_by_vowel/a/m8_long_trillo_a.wav"]}
