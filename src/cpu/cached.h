/* SPDX-License-Identifier: MPL-2.0 */
#ifndef HCGBA_CACHED_H
#define HCGBA_CACHED_H
#include "decode.h"
#include "gba.h"
struct HcDecoded {
    struct Inst in;
    void (*native)(struct CPU*);
    bool valid;
};
struct HcDecoded* hcCachedDecode(struct GBA*, uint32_t address, uint32_t opcode, bool thumb);
void stepCachedInstruction(struct GBA*);
bool hcNativeAvailable(const struct GBA*);
#endif
