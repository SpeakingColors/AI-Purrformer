"""Pull the long-tone clips of one vowel out of the VocalSet zip, whatever it takes. AGPL-3.0.

The zip's index is damaged (python's zipfile and unzip both stumble), so we try several readers and
keep the first that finds enough singers. Prints GitHub notices so a run explains itself.
Usage: python extract_vocalset.py VocalSet.zip OUTDIR [vowel]
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

NEED = 15  # VocalSet has 20 singers; fewer than this means the reader missed files


def note(msg: str) -> None:
    print("::notice title=extract::" + msg.replace("%", "%25").replace("\n", "%0A"), flush=True)


def singers(out: Path) -> set:
    return {m.group(1) for p in out.rglob("*.wav") for m in [re.match(r"([fm]\d+)_long", p.name)] if m}


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    note(f"{cmd[0]} exit {r.returncode}: {(r.stderr or r.stdout)[-300:]}")


def via_unzip(z, out, v):
    if shutil.which("unzip"):
        run(["unzip", "-q", "-o", z, f"data_by_vowel/{v}/*_long_*", "-d", str(out)])


def via_bsdtar(z, out, v):
    exe = shutil.which("bsdtar")
    if exe:
        run([exe, "-xf", z, "-C", str(out), "--include", f"data_by_vowel/{v}/*_long_*"])


def via_7z(z, out, v):
    exe = shutil.which("7z") or shutil.which("7zz")
    if exe:
        run([exe, "x", z, f"-o{out}", f"-ir!data_by_vowel/{v}/*_long_*", "-y"])


def via_stream(z, out, v):
    """Read local file headers one after another, ignoring the (damaged) index."""
    from stream_unzip import stream_unzip  # pip install stream-unzip
    want = re.compile(rf"data_by_vowel/{v}/[fm]\d+_long_.*_{v}\.wav$")

    def chunks():
        with open(z, "rb") as f:
            while True:
                c = f.read(1 << 20)
                if not c:
                    return
                yield c

    for name, _size, parts in stream_unzip(chunks()):
        n = name.decode("utf-8", "replace")
        if want.search(n) and "__MACOSX" not in n:
            dest = out / n
            dest.parent.mkdir(parents=True, exist_ok=True)
            with open(dest, "wb") as fh:
                for c in parts:
                    fh.write(c)
        else:
            for _ in parts:
                pass


def main(argv) -> int:
    z, out = argv[1], Path(argv[2])
    v = argv[3] if len(argv) > 3 else "a"
    out.mkdir(parents=True, exist_ok=True)
    for fn in (via_unzip, via_bsdtar, via_7z, via_stream):
        try:
            fn(z, out, v)
        except Exception as exc:  # try the next reader
            note(f"{fn.__name__} failed: {exc}")
        n = len(singers(out))
        note(f"after {fn.__name__}: {n} singers, {len(list(out.rglob('*.wav')))} clips")
        if n >= NEED:
            return 0
    print(f"::error title=extract::only {len(singers(out))} singers found with any reader")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
