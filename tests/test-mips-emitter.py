"""Execute the actual emitted MIPS32r2 instructions against HCGBA stepExecute.
Requires a compiled mips-alu-host shared library and Unicorn 2.x (test runner
only; no Unicorn source is incorporated into the emulator).
Usage: python tests/test-mips-emitter.py path/to/mips-alu-host.dll
"""
import ctypes as C
import random
import struct
import sys
from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_LITTLE_ENDIAN
from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_RA, UC_MIPS_REG_S0, UC_MIPS_REG_GP, UC_MIPS_REG_SP

dll = C.CDLL(sys.argv[1])
u32 = C.c_uint32
dll.hcTest.argtypes = [u32, C.c_int, C.POINTER(u32), u32, C.POINTER(u32), C.POINTER(u32)]
dll.hcTest.restype = C.c_size_t
rng = random.Random(0x4187FC91)
edges = [0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0x80000001]
uc = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 | UC_MODE_LITTLE_ENDIAN)
BASE, DATA, STOP = 0x100000, 0x200000, 0x101000
uc.mem_map(BASE, 0x2000)
uc.mem_map(DATA, 0x1000)
code, expected = (u32 * 128)(), (u32 * 17)()
count = 0

def run(op, thumb, trial=0):
    global count
    regs = (u32 * 16)(*[edges[(r+trial)%6] if trial<6 else rng.getrandbits(32) for r in range(16)])
    cpsr = (rng.getrandbits(4)<<28) | 0x1f | (0x20 if thumb else 0)
    words = dll.hcTest(op, thumb, regs, cpsr, code, expected)
    if not words:
        return
    emitted = bytes(code)[:words*4]
    uc.mem_write(BASE, emitted)
    # Explicitly discard cached translated blocks when replacing generated code.
    uc.ctl_remove_cache(BASE, BASE+0x1000)
    uc.mem_write(DATA, bytes(regs)+struct.pack("<I", cpsr))
    uc.reg_write(UC_MIPS_REG_A0, DATA)
    uc.reg_write(UC_MIPS_REG_RA, STOP)
    for r in [UC_MIPS_REG_S0, UC_MIPS_REG_GP, UC_MIPS_REG_SP]:
        uc.reg_write(r, 0x12345678)
    uc.emu_start(BASE, STOP, count=256)
    actual = bytes(uc.mem_read(DATA, 68))
    if actual != bytes(expected):
        raise AssertionError(f"MIPS mismatch {op:08x} thumb={thumb} trial={trial}\n"
                             f"actual={struct.unpack('<17I', actual)}\nexpected={list(expected)}")
    for r in [UC_MIPS_REG_S0, UC_MIPS_REG_GP, UC_MIPS_REG_SP]:
        assert uc.reg_read(r) == 0x12345678, "o32 preserved register corrupted"
    count += 1

# Every Thumb encoding eligible for this native path.
for op in range(65536):
    run(op, True, op % 8)
# Every ALU op, S bit, immediate shift type/amount, with edge and random registers.
for op in range(16):
    for flags in range(2):
        for shift in range(4):
            for amount in range(32):
                base = 0xe0000000 | op<<21 | flags<<20 | 1<<16 | 2<<12
                for trial in range(8):
                    run(base | amount<<7 | shift<<5 | 3, False, trial)
                run(base | 0x02000000 | amount<<7 | rng.randrange(256), False, 7)
# Register aliasing and PC reads.
for n in range(20000):
    run((0xe0000000 | rng.getrandbits(26)) & ~0x10, False, 7)
print(f"PASS: {count} emitted MIPS ALU executions match HCGBA's exact CPU")
