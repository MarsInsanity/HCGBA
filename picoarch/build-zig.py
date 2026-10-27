#!/usr/bin/env python3
"""Portable fallback cross-build; Zig 0.13.0 is the validated version.
Uses the existing core sources only. Produces MIPS32r2 o32 hard-float ELF.
The SDK build remains available for reproducing the firmware's GCC environment.
"""
import argparse
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--zig", default="zig", help="path to Zig 0.13.0 executable")
parser.add_argument("--output", default="build-zig/hcgba_libretro.so")
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
cmake = (root / "CMakeLists.txt").read_text()
sources = re.search(r"set\(CORE_SOURCES\s+(.*?)\n\)", cmake, re.S).group(1).split()
sources = [s if s != "src/video/render-thread.c" else "src/video/render-single.c" for s in sources]
out = pathlib.Path(args.output).resolve()
out.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([
    args.zig, "cc", "-target", "mipsel-linux-gnueabihf.2.19", "-mcpu=mips32r2",
    "-shared", "-fPIC", "-std=gnu11", "-O3", "-s", "-fno-strict-aliasing",
    "-DHCGBA_CACHED_CPU=1", "-Isrc", "-Ilibretro", *sources,
    "src/cpu/cached.c", "src/cpu/mips32/emit.c", "picoarch/libretro.c",
    "-lm", "-Wl,--version-script=libretro/link.T", "-Wl,--no-undefined",
    "-o", str(out)
], cwd=root, check=True)
print(out)
