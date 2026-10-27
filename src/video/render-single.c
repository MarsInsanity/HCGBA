/* SPDX-License-Identifier: MPL-2.0
 * Single-thread frontend: rendering remains synchronous in render.c.
 * Logging hooks have no worker to notify; no 64-bit atomics or log buffer. */
#include "gba.h"
bool renderThreadStart(struct GBA* gba, bool late) { return false; }
void renderThreadStop(struct GBA* gba) {}
void renderThreadClone(struct GBA* gba) {}
const struct Renderer* renderThreadRenderer(struct GBA* gba) { return &gba->renderer; }
void renderThreadRegister(struct GBA* gba, uint32_t address, uint16_t value) {}
void renderThreadScanline(struct GBA* gba, int y) { renderScanline(gba,y); }
void renderThreadStartLine(struct GBA* gba, int y) {}
void renderThreadLatch(struct GBA* gba) {}
void renderThreadFinishFrame(struct GBA* gba) {}
