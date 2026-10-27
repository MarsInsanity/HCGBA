/* SPDX-License-Identifier: MPL-2.0
 * Cached execution of HCGBA's existing decoded instructions. MIPS ALU
 * instructions are translated to native leaf functions; other instructions
 * retain the exact interpreter's bus/timing behavior. */
#include "cached.h"
#include "jit.h"
#include "mips32/emit.h"
#include <stdlib.h>
#include <string.h>
#if defined(__mips__) && defined(__linux__) && __SIZEOF_POINTER__ == 4 && !defined(__mips16)
#include <sys/mman.h>
#define HC_NATIVE_MIPS 1
#else
#define HC_NATIVE_MIPS 0
#endif

enum { CACHE_ENTRIES = 1024, CODE_BYTES = CACHE_ENTRIES * HC_MIPS_WORDS * 4 };
struct Jit {
    struct HcDecoded entries[CACHE_ENTRIES];
    uint32_t* code;
};

bool jitInit(struct GBA* gba) {
    gba->jit = calloc(1,sizeof(*gba->jit));
    // A memory-constrained device can always use the original interpreter.
    if (!gba->jit) return true;
#if HC_NATIVE_MIPS
    if (!getenv("HCGBA_NO_NATIVE")) {
        void* code = mmap(NULL,CODE_BYTES,PROT_READ|PROT_WRITE|PROT_EXEC,
                          MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if (code != MAP_FAILED) gba->jit->code = code;
    }
#endif
    return true;
}
bool hcNativeAvailable(const struct GBA* gba) {
    return gba->jit && gba->jit->code;
}
void jitDeinit(struct GBA* gba) {
    if (!gba->jit) return;
#if HC_NATIVE_MIPS
    if (gba->jit->code) munmap(gba->jit->code,CODE_BYTES);
#endif
    free(gba->jit);
    gba->jit = NULL;
}

struct HcDecoded* hcCachedDecode(struct GBA* gba, uint32_t address, uint32_t opcode, bool thumb) {
    struct Jit* j=gba->jit;
    if (thumb) opcode &= 0xffff;
    unsigned slot=((address>>1) ^ (address>>12) ^ (opcode>>16) ^ thumb) & (CACHE_ENTRIES-1);
    struct HcDecoded* e=&j->entries[slot];
    if (e->valid && e->in.address==address && e->in.opcode==opcode && e->in.thumb==thumb) return e;
    memset(e,0,sizeof(*e));
    if (thumb) decodeThumb(&e->in,address,(uint16_t)opcode);
    else decodeArm(&e->in,address,opcode);
#if HC_NATIVE_MIPS
    if (j->code) {
        uint32_t* code=j->code+slot*HC_MIPS_WORDS;
        size_t words=hcMipsEmit(code,HC_MIPS_WORDS,&e->in);
        if (words) {
            // MIPS has separate data/instruction caches: publish all emitted bytes.
            __builtin___clear_cache((char*)code,(char*)(code+words));
            e->native=(void (*)(struct CPU*))code;
        }
    }
#endif
    e->valid=true;
    return e;
}

void jitRunLoop(struct GBA* gba) {
    uint32_t frame=gba->video.frameCounter;
    while (gba->video.frameCounter==frame && !gba->returnRequested) {
        if (gba->cpu.halted) {
            if (gba->cpu.cycles < gba->cpu.nextEvent) gba->cpu.cycles=gba->cpu.nextEvent;
            processEvents(gba);
        } else stepCachedInstruction(gba);
    }
    gba->returnRequested=false;
}
void jitFlush(struct GBA* gba) {
    if (gba->jit) for (unsigned i=0;i<CACHE_ENTRIES;++i) {
        // A mapper can invalidate while stepExecute still holds &entry->in.
        // Preserve that decoded instruction until the next dispatch.
        gba->jit->entries[i].valid=false;
        gba->jit->entries[i].native=NULL;
    }
}
void jitStateLoaded(struct GBA* gba, bool changed) { jitFlush(gba); }
void jitRomChanged(struct GBA* gba) { jitFlush(gba); }
void jitForgetRewrites(struct GBA* gba) { jitFlush(gba); }
// Every dispatch validates the fetched opcode. No memory write can leave a
// stale translation, and prefetched stale opcodes still execute correctly.
void jitInvalidate(struct GBA* gba, uint32_t address, uint32_t size) {}
void jitCodeWritten(struct GBA* gba, uint32_t address, int size) {}
bool jitCovers(struct GBA* gba, uint32_t address) { return false; }
void jitSyncPc(struct GBA* gba) {}
int32_t jitFetchGap(struct GBA* gba) { return -1; }
void jitWaitstatesChanged(struct GBA* gba, int32_t gap) {}
void jitIdleRead(struct GBA* gba, uint32_t address, int size, uint32_t value) {}
void jitIdleWrite(struct GBA* gba, uint32_t address, uint32_t value, int size) {}
