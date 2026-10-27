# Validation record — 27 October 2026

Upstream reviewed and integrated through `1f97faee6f1256ed97296be7050a26d85ccd2fb4`.

## Completed

- 70,973 executions of emitted MIPS32 machine code matched this tree's exact ALU.
  The host adapter compiled the real emitter and `stepExecute`; Unicorn 2.1.4
  executed the generated words. Unicorn is a test dependency only, not part of
  the core and not a source of implementation code.
- 366,720 cached ALU comparisons passed on the x86-64 host.
- ARM/Thumb instruction lockstep, bus/timer/IRQ scheduling, RAM rewriting,
  stale-prefetch handling, and state reload passed against the existing CPU.
- Regression tests passed for mapper invalidation preserving an in-flight
  decoded instruction and upstream's removal of exit scratch from states.
- Libretro dynamic loading, RGB565 video, 32768 Hz audio, save-memory exposure,
  reset, unaligned save-state buffers, and file-path ROM loading passed.
- The optimized complete MIPS32 core linked successfully with Zig 0.13.0.
- CMake also cross-built the core and both Linux MIPS test executables.
- ELF inspection confirmed ELF32 little-endian, MIPS32r2, o32, hard-float,
  32-bit FPRs. The core imports GLIBC_2.0 and GLIBC_2.2 symbols and depends on
  libc.so.6 and libpthread.so.0. All non-weak undefined symbols were found in
  the pinned SF3000 SDK's glibc 2.20 export table.

The Windows host tests used temporary adapters for POSIX time/file/dynamic
loading APIs. These adapters are not compiled into the handheld core.
The source's CTest suite is intended to run on Linux and under qemu-mipsel.

GitHub Actions run [40587028462](https://github.com/MarsInsanity/HCGBA/actions/runs/40587028462)
also passed the Linux host, exact-interpreter, QEMU native-MIPS and disabled-native
tests, and built the core using the pinned SF3000 GCC SDK. This run validated
commit `a52940a`; 74Kc tuning and the benchmark were added subsequently.

## Not established

- Actual SF3000/R36S loading and performance.
- Hamtaro gameplay or any commercial-game compatibility percentage.
- Faster execution or better compatibility than mGBA.
- Full-library coverage, real-device instruction-cache behavior, or sustained
  audio behavior on the handheld.
- Multiplayer behavior of the inherited full frontend.


The downloadable core is an experimental test build, not a performance-certified
release. Its build command is reproducible with `picoarch/build-zig.py`.
