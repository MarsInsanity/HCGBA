/* SPDX-License-Identifier: MPL-2.0
 * Host-only adapter for executing the emitter's words in a MIPS CPU test
 * runner. The reference is stepExecute from this source tree. All timing
 * helpers abort: hcMipsEmit must reject every instruction requiring one. */
#include "gba.h"
#include "cpu/decode.h"
#include "cpu/mips32/emit.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif
EXPORT size_t hcTest(uint32_t opcode,int thumb,const uint32_t* regs,
                    uint32_t cpsr,uint32_t* code,uint32_t* expected) {
    static struct GBA gba;
    struct Inst in;
    if(thumb) decodeThumb(&in,0x03000100,(uint16_t)opcode);
    else decodeArm(&in,0x03000100,opcode);
    size_t n=hcMipsEmit(code,HC_MIPS_WORDS,&in);
    if(!n) return 0;
    memcpy(gba.cpu.r,regs,64);
    gba.cpu.cpsr=cpsr;
    stepExecute(&gba,&in);
    memcpy(expected,gba.cpu.r,64);
    expected[16]=gba.cpu.cpsr;
    return n;
}
void memorySetActiveRegion(struct GBA* g,uint32_t a) { abort(); }
bool busRefillCartridge(struct GBA* g,uint32_t a,bool t) { abort(); }
uint32_t busFetch(struct GBA* g,uint32_t a,bool t) { abort(); }
uint16_t codeFetch16(struct GBA* g,uint32_t a) { abort(); }
uint32_t codeFetch32(struct GBA* g,uint32_t a) { abort(); }
void cpuSetPrivilegeMode(struct GBA* g,unsigned m) { abort(); }
bool cpuModeHasSpsr(unsigned m) { abort(); }
void cpuWriteCpsr(struct GBA* g,uint32_t p) { abort(); }
void busIdle(struct GBA* g) { abort(); }
bool cpuMultiplyCarryFull(uint32_t v) { abort(); }
bool cpuMultiplyCarryLow(uint32_t a,uint32_t b,uint32_t c) { abort(); }
bool cpuMultiplyCarryHigh(uint32_t a,uint32_t b,uint32_t c,bool s) { abort(); }
uint32_t busRead(struct GBA* g,uint32_t a,int s,bool q) { abort(); }
void busWrite(struct GBA* g,uint32_t a,uint32_t v,int s,bool q) { abort(); }
void jitHelperMsr(struct GBA* g,uint32_t v,uint32_t p) { abort(); }
