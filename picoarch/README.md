# HCGBA for SF3000 / R36S V2.6 picoarch

This port builds the existing HCGBA GBA machine for little-endian MIPS32r2
(o32, hard-float, 32-bit FPRs). It does not import another emulator core.

## What runs

- Existing GBA CPU, renderer, audio, BIOS, memory, DMA, interrupts and save-chip code.
- An opcode-validated decoded-instruction cache. Eligible ARM/Thumb arithmetic,
  logic, immediate shifts and flags compile to MIPS32r2 leaf functions.
- Register shifts, PC writes, branches, memory operations, multiply and system
  instructions retain the existing exact execution path.
- RGB565 240x160 video, native 32768 Hz audio, all ten GBA buttons, reset,
  SRAM/Flash/EEPROM persistence and save states through libretro.
- Optional `gba_bios.bin` from picoarch's system directory; otherwise this
  repository's existing open-source BIOS is used.

This is an initial port, not a claim of full speed across the GBA library.
No commercial-game compatibility percentage or advantage over mGBA has been
measured. Hamtaro and actual SF3000/R36S performance still need device tests.
The inherited README's accuracy and speed figures describe upstream targets,
not this MIPS port.

## Build for the handheld

Use the SDK named by TreeFrogUI_picoarch's build scripts:

```sh
export CROSS_COMPILE=/path/to/sdk/opt/ext-toolchain/bin/mips-mti-linux-gnu-
export SF3000_SYSROOT=/path/to/sdk/mipsel-buildroot-linux-gnu/sysroot
sh picoarch/build-sf3000.sh
```

The output is `build-sf3000/hcgba_libretro.so`.

A portable fallback, validated with Zig 0.13.0, is:

```sh
python3 picoarch/build-zig.py --zig /path/to/zig
```

It emits the same MIPS32r2/o32/hard-float ABI, using glibc 2.19 symbol
versions. Its imports have been checked against the SDK's glibc 2.20.
The supplied test binary was built this way; the actual handheld has not
been tested. The GitHub Actions workflow
builds with a pinned SF3000 SDK and packages the core. Its separate Debian MIPS
build is only for QEMU tests: a generic Linux MIPS library can require a newer
libc than the handheld, so do not deploy that test build.

LTO is off for MIPS because TreeFrogUI reports MIPS PIC/ABICALLS failures with
it. DSP extensions, MSA, NEON, and soft-float are not required. The CPU code is
integer-only; hard-float here is an ABI requirement of the frontend/toolchain.

## Launch through picoarch

Place the core alongside the other picoarch cores and use the same picoarch
binary and working directory as your firmware's existing GBA launcher:

```sh
./picoarch ./hcgba_libretro.so "/path/to/game.gba"
```

Use a separate launcher entry first. Keep the existing core and save backups.
Do not rename HCGBA to gpsp: select the new core path in the launcher instead.
Firmware layouts differ, so no script overwrites your firmware paths.

Options use picoarch's legacy libretro variables:

- **CPU execution:** cached (default) or exact, useful for comparing a problem.
- **Skip BIOS intro:** disabled (default) or enabled; applies on reset/load.

The frontend exposes a stable 128 KiB save buffer before the first frame so
picoarch can restore saves before save-chip autodetection. Existing shorter
SRAM/EEPROM saves occupy its beginning. Make backups before changing cores.
Save states are tied to this code's state format; gpSP/mGBA states are not
interchangeable. Cheating, GB/GBC, network/link UI, rumble and motion/solar
control mapping are not exposed by this compact frontend.

## Execution design

The exact CPU still performs each instruction's fetch and all bus timing.
The cache key contains address, ARM/Thumb mode, and the *prefetched* opcode.
Self-modifying code therefore cannot reuse an incorrect translation, and the
two stale prefetched instructions still execute as originally fetched.
WAITCNT changes use the current bus implementation, without baked-in timing.

Generated code uses caller-saved GPRs only, leaves GP/SP/RA intact, emits a
return delay slot, and flushes the MIPS instruction cache before publication.
The cache has 1024 decoded slots and a 512 KiB executable arena. If executable
mapping is unavailable it uses cached interpretation; if allocation fails,
the original interpreter still runs. `HCGBA_NO_NATIVE=1` disables code generation
for diagnostics. The cache is single-threaded and is cleared on state reload.

The port translates ALU operations rather than whole linked basic blocks.
Per-instruction dispatch and exact bus timing remain performance costs.
Do not interpret the presence of a native emitter as a speed guarantee.

## Validation

```sh
cmake -S . -B build-test -DHCGBA_PICOARCH=ON -DHCGBA_CACHED_CPU=ON -DHCGBA_LTO=OFF
cmake --build build-test --parallel
ctest --test-dir build-test --output-on-failure
```

The CPU test compares eligible Thumb encodings and generated ARM arithmetic
cases against this tree's exact interpreter, including register aliasing,
carry/borrow, overflow and shift boundaries. On MIPS it requires actual native
execution unless explicitly disabled. It also checks ARM/Thumb lockstep,
interrupt scheduling, RAM self-modification, stale prefetch and state reload.

The libretro test dynamically loads the core and checks ROM loading, video,
audio, save RAM, reset, unaligned save-state buffers, and the file-path loader.
No copyrighted ROMs and no other emulator source are needed for these tests.

For Hamtaro, record the exact game title/region, ROM hash, firmware, BIOS choice,
CPU option, failing scene and a save immediately before it. Measure frame
time/audio stability on the device using both cached and exact modes. Passing
synthetic tests proves neither a Hamtaro fix nor full-library compatibility.

## Source and licenses

Upstream updates through `1f97fae` are integrated; see [UPSTREAM.md](UPSTREAM.md).

Initial base: MarsInsanity/HCGBA commit
`132c881d8f00b758dd87b3ea213f08fc1b0911b7`.
The repository already contains mGBA-derived code; all notices are retained.
New port files are MPL-2.0. The compact GBA-only target does not link Gambatte;
the existing full target and its licenses are unchanged.

Build reference:
https://github.com/tzubertowski/TreeFrogUI_picoarch/tree/r36sx
