# Upstream review — 27 October 2026

Merged Jacob-Matthew-Cook/armGBA main through
`1f97faee6f1256ed97296be7050a26d85ccd2fb4`, ten commits after the fork's
`132c881d8f00b758dd87b3ea213f08fc1b0911b7`. The merge preserves upstream's
commit history and the MIPS port.

| Commit | Change | Effect on HCGBA |
| --- | --- | --- |
| 666fa70 | Netplay labels, help, bounded discovery names | Retained in full frontend |
| d70aa5f | armGBA 1.2 version metadata | Retained in original targets |
| 72f2cb9 | Portable GB boot-ROM CRC integer type | Retained for full-core cross compilation |
| 345f372 | Multiple local controllers in netplay | Retained in full frontend |
| 8b139f5 | Per-player controller assignment and persistence | Retained in full frontend |
| 52c7f80 | Join without loaded game; find matching local game | Retained in full frontend |
| bda28f0 | Delay premature link mismatch messages | Retained in original libretro frontend |
| 93d277c | Explicit local-host scan UI | Retained in full frontend |
| de9788e | Pick a renamed local copy of the host's game | Retained in full frontend |
| 1f97fae | Exclude recompiler exit scratch from saved state | Used by the compact MIPS core too |

The compact picoarch frontend runs one GBA and does not expose the standalone
netplay/controller UI. Those changes are preserved in the repository without
adding their runtime cost to the handheld target.

The state fix has a regression test in `tests/hcgba-cpu.c`: changing exitPc,
exitOps, exitReason and exitFlags must not change serialized machine bytes.
Upstream's full frontend/netplay changes were reviewed but not multiplayer
tested in this environment.
