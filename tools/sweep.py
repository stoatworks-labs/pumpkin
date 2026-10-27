#!/usr/bin/env python3
"""Render every parameter at both ends of its range and fail if any made no
difference.

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op
-- so a slider can be stone dead while everything compiles, links, loads and
renders.

Each control is swept where it can act: the faces, Seed and Expressions on
small tiles (32 of them at 320x180, so every face is somewhere and some face
is mid-blink). An option parameter reads back 0..1 whatever its count (the
fleet's trap), so options are set here by element index. A control whose
ends differ by less than `--floor` (mean 8-bit difference per channel) is
reported as barely alive: vectrix's lesson, a control can be alive, correct
and still useless.

Every render is 320x180 (CI's raster) and 160 frames at 60 fps, unless a
control names its own frame count.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N]
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

# name -> (low setting, high setting, context settings, extra flags)
SMALL = ["Tile Width=40", "Tile Height=45"]

SWEEP = {
    "Tile Width": ("40", "80", [], []),
    "Tile Height": ("45", "90", [], []),
    "Origin X": ("0", "13", [], []),
    "Origin Y": ("0", "13", [], []),
    "Seed": ("1", "2", SMALL, []),
    "Classic": ("1", "0", SMALL, []),
    "Fierce": ("1", "0", SMALL, []),
    "Happy": ("1", "0", SMALL, []),
    "Surprised": ("1", "0", SMALL, []),
    "Sly": ("1", "0", SMALL, []),
    "Goofy": ("1", "0", SMALL, []),
    "Flicker": ("0", "1", [], []),
    # A blink is brief: at the time the fixtures found busiest for these
    # tiles (tiny-320x180: seed 7, 6.2 s, frame 373) several faces are mid-way.
    "Expressions": ("0", "1", SMALL + ["Seed=7"], ["frames=373"]),
    "Speed": ("0.3", "0.7", [], []),
    # Wall reads this machine's clock, years into the test card's endless
    # time; Resolume's is 2.65 s in.
    "Clock": ("0", "1", [], []),
    "Background": ("0", "2", [], []),
}

# Parameters with no pixel to sweep: the About block (a text line and browser
# buttons).
SKIP = {"About", "Project page", "Source on GitHub", "Support the work", "User guide"}


def read_png(path):
    """Enough of PNG for pktest's own writer: 8-bit RGBA, filter 0 rows."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        out += raw[start + 1:start + 1 + stride]
    return bytes(out)


def difference(a, b):
    if len(a) != len(b):
        return 255.0
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def render(pktest, out, settings, extra):
    frames = next((e.split("=")[1] for e in extra if e.startswith("frames=")), "160")
    args = [str(pktest), "--out", str(out), "--size", "320x180", "--frames", frames]
    for setting in settings:
        args += ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"pktest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def parameters(pktest):
    result = subprocess.run([str(pktest), "--list"], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"pktest --list failed: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines()[1:]:
        parts = re.split(r"\s{2,}", line.strip())
        if len(parts) >= 3 and parts[0].isdigit():
            names.append(parts[1].strip())
    return names


def sweep_one(pktest, scratch, name):
    low, high, context, extra = SWEEP[name]
    tag = abs(hash(name))
    before = render(pktest, scratch / f"{tag}-a.png", context + [f"{name}={low}"], extra)
    after = render(pktest, scratch / f"{tag}-b.png", context + [f"{name}={high}"], extra)
    return name, difference(before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--floor", type=float, default=0.05,
                        help="mean 8-bit difference below which a control is 'barely alive'")
    args = parser.parse_args()

    build = args.build if args.build.is_absolute() else REPO / args.build
    pktest = build / "pktest"
    if not pktest.exists():
        print(f"{pktest} not found", file=sys.stderr)
        return 1

    declared = parameters(pktest)
    unknown = [n for n in declared if n not in SWEEP and n not in SKIP]
    if unknown:
        # A new parameter with no sweep is a hole, not a pass.
        print(f"no sweep defined for: {', '.join(unknown)}", file=sys.stderr)
        return 1
    unused = [n for n in SWEEP if n not in declared]
    if unused:
        print(f"the sweep names parameters the plugin does not have: {', '.join(unused)}", file=sys.stderr)
        return 1

    dead, weak = [], []
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(lambda n: sweep_one(pktest, scratch, n), [n for n in declared if n in SWEEP]))
    for name, delta in results:
        if delta == 0.0:
            dead.append(name)
            print(f"  DEAD {name:16s} both ends identical")
        elif delta < args.floor:
            weak.append(name)
            print(f"  WEAK {name:16s} mean delta {delta:.4f}")
        elif args.verbose:
            print(f"  ok   {name:16s} mean delta {delta:.3f}")

    print(f"{len(results)} parameters swept, {len(dead)} dead, {len(weak)} barely alive")
    if dead or weak:
        print("\nA parameter that changes nothing is usually a uniform name that does not match\n"
              "the C++, or a setting nothing reads. Both are silent everywhere else.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
