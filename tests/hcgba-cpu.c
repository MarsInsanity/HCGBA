/* SPDX-License-Identifier: MPL-2.0
 * Differential tests against this repository's own exact CPU. No game ROMs
 * or external emulator source are needed. Native tests run under qemu-mipsel. */
#include "gba.h"
#include "cpu/decode.h"
#include "cpu/jit.h"
#ifdef HCGBA_CACHED_CPU
#include "cpu/cached.h"
#include "cpu/mips32/emit.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static uint32_t seed=0x4187fc91;
static uint32_t random32(void) { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed; }
static unsigned long cases, nativeCases;
#ifdef HCGBA_CACHED_CPU
static void aluCase(struct GBA* fast,struct GBA* ref,uint32_t op,bool thumb) {
    struct Inst decoded;
    if (thumb) decodeThumb(&decoded,0x03000100,(uint16_t)op);
    else decodeArm(&decoded,0x03000100,op);
    uint32_t code[HC_MIPS_WORDS];
    size_t words=hcMipsEmit(code,HC_MIPS_WORDS,&decoded);
    if (!words) return;
    CHECK(words<=HC_MIPS_WORDS);
    struct HcDecoded* entry=hcCachedDecode(fast,decoded.address,op,thumb);
    CHECK(!memcmp(&entry->in,&decoded,sizeof(decoded)));
    CHECK(entry==hcCachedDecode(fast,decoded.address,op,thumb));
    static const uint32_t edges[]={0,1,0x7fffffff,0x80000000,0xffffffff,0x80000001};
    for (unsigned trial=0;trial<8;++trial) {
        memset(&fast->cpu,0,sizeof(fast->cpu));
        for (unsigned r=0;r<16;++r)
            fast->cpu.r[r]=trial<6 ? edges[(r+trial)%6] : random32();
        fast->cpu.cpsr=(random32()&0xf0000000)|MODE_SYSTEM|(thumb?PSR_T:0);
        ref->cpu=fast->cpu;
        if (entry->native) { entry->native(&fast->cpu); ++nativeCases; }
        else stepExecute(fast,&entry->in);
        stepExecute(ref,&decoded);
        if (memcmp(&fast->cpu,&ref->cpu,sizeof(fast->cpu))) {
            fprintf(stderr,"ALU mismatch opcode=%08x thumb=%u trial=%u cpsr=%08x/%08x\n",
                op,thumb,trial,fast->cpu.cpsr,ref->cpu.cpsr);
            for(int r=0;r<16;++r) fprintf(stderr,"r%d=%08x/%08x ",r,fast->cpu.r[r],ref->cpu.r[r]);
            exit(1);
        }
        ++cases;
    }
}
#endif
static void fastStep(struct GBA* gba) {
#ifdef HCGBA_CACHED_CPU
    stepCachedInstruction(gba);
#else
    stepInstruction(gba);
#endif
}
static void same(struct GBA* a,struct GBA* b) {
    CHECK(!memcmp(&a->cpu,&b->cpu,sizeof(a->cpu)));
    CHECK(!memcmp(a->io,b->io,sizeof(a->io)));
    CHECK(!memcmp(a->ewram,b->ewram,sizeof(a->ewram)));
    CHECK(!memcmp(a->iwram,b->iwram,sizeof(a->iwram)));
    CHECK(a->video.frameCounter==b->video.frameCounter);
    CHECK(timingCurrentTime(a)==timingCurrentTime(b));
}
static void lockstep(struct GBA* a,struct GBA* b) {
    // Original hand-written ARM program. RAM read/write, arithmetic, branch,
    // and a Thumb loop reached through BX.
    uint32_t rom[256]={0xe3a00402,0xe3a01000,0xe2911001,0xe5801000,
        0xe5902000,0xe3520080,0x1afffffa,0xe28f3001,0xe12fff13,
        0x39013101,0xe7fc6001};
    CHECK(gbaLoadRom(a,rom,sizeof(rom)) && gbaLoadRom(b,rom,sizeof(rom)));
    CHECK(gbaLoadBios(a,armgbaBios,SIZE_BIOS) && gbaLoadBios(b,armgbaBios,SIZE_BIOS));
    gbaReset(a,true); gbaReset(b,true);
    for(unsigned n=0;n<30000;++n) {
        if(n==1000) {
            // A running timer and IRQ exercise instruction/event boundaries.
            ioWrite(a,REG_TM0CNT_LO,0xfff0); ioWrite(b,REG_TM0CNT_LO,0xfff0);
            ioWrite(a,REG_TM0CNT_HI,0xc0); ioWrite(b,REG_TM0CNT_HI,0xc0);
            gbaWriteIe(a,1<<IRQ_TIMER0); gbaWriteIe(b,1<<IRQ_TIMER0);
            gbaWriteIme(a,1); gbaWriteIme(b,1);
        }
        fastStep(a); stepInstruction(b);
        same(a,b);
    }
    // Execute from writable RAM, then overwrite an instruction already in
    // the pipeline. Its old value must run once before the new one is fetched.
    uint32_t ram[]={0xe3a00011,0xe3a01022,0xeafffffc};
    memcpy(a->iwram,ram,sizeof(ram)); memcpy(b->iwram,ram,sizeof(ram));
    a->cpu.cpsr=b->cpu.cpsr=MODE_SYSTEM|PSR_I;
    a->cpu.r[15]=b->cpu.r[15]=0x03000000;
    stepReload(a); stepReload(b);
    uint32_t changed=0xe3a00077;
    memcpy(a->iwram,&changed,4); memcpy(b->iwram,&changed,4);
    for(int n=0;n<100;++n) { fastStep(a); stepInstruction(b); same(a,b); }
    CHECK(a->cpu.r[0]==0x77);
    void* state=malloc(gbaStateSize());
    CHECK(state);
    gbaSaveState(a,state);
    // Upstream 1f97fae: host recompiler exit scratch cannot affect state bytes.
    void* scratchState=malloc(gbaStateSize()); CHECK(scratchState);
    a->exitPc=0x12345678; a->exitOps[0]=0x87654321; a->exitOps[1]=0x11223344;
    a->exitReason=3; a->exitFlags=0xf0000000;
    gbaSaveState(a,scratchState);
    CHECK(!memcmp(state,scratchState,gbaStateSize()));
    free(scratchState);
    CHECK(gbaLoadState(a,state,gbaStateSize()));
    CHECK(gbaLoadState(b,state,gbaStateSize()));
    free(state);
    for(int n=0;n<100;++n) { fastStep(a); stepInstruction(b); same(a,b); }
}
int main(void) {
    struct GBA* a=gbaCreate(); struct GBA* b=gbaCreate();
    CHECK(a && b);
#ifdef HCGBA_CACHED_CPU
#if defined(__mips__)
    if (!getenv("HCGBA_NO_NATIVE")) CHECK(hcNativeAvailable(a));
#endif
    for(uint32_t op=0;op<65536;++op) aluCase(a,b,op,true);
    // All operations, flag settings, immediate rotations and shift amounts.
    for(unsigned op=0;op<16;++op) for(unsigned s=0;s<2;++s)
        for(unsigned shift=0;shift<4;++shift) for(unsigned amount=0;amount<32;++amount) {
            uint32_t base=0xe0000000|(op<<21)|(s<<20)|(1<<16)|(2<<12);
            aluCase(a,b,base|(amount<<7)|(shift<<5)|3,false);
            aluCase(a,b,base|0x02000000|(amount<<7)|(random32()&255),false);
        }
    for(unsigned n=0;n<20000;++n) {
        // Include aliases and PC reads, but no register-shift encodings.
        uint32_t op=0xe0000000|(random32()&0x03ffffff);
        op&=~0x10u;
        aluCase(a,b,op,false);
    }
#endif
    #ifdef HCGBA_CACHED_CPU
    struct HcDecoded* inFlight=hcCachedDecode(a,0x08000000,0xe3a01055,false);
    struct Inst preserved=inFlight->in;
    jitRomChanged(a);
    CHECK(!inFlight->valid && !memcmp(&preserved,&inFlight->in,sizeof(preserved)));
    #endif
    lockstep(a,b);
    printf("PASS: %lu ALU cases (%lu native); ARM/Thumb bus+IRQ lockstep, stale prefetch, state reload\n",cases,nativeCases);
    gbaDestroy(a); gbaDestroy(b);
    return 0;
}
