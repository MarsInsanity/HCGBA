/* SPDX-License-Identifier: MPL-2.0 */
#ifndef HCGBA_MIPS_EMIT_H
#define HCGBA_MIPS_EMIT_H
#include "../decode.h"
#include <stddef.h>
enum { HC_MIPS_WORDS = 128 };
/* Emit an o32 leaf function void(struct CPU*). Zero means exact-CPU fallback.
 * Uses only caller-saved GPRs, no stack, FPU, GP or helper calls. */
size_t hcMipsEmit(uint32_t* code, size_t capacity, const struct Inst*);
#endif
