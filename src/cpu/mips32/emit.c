/* SPDX-License-Identifier: MPL-2.0
 * MIPS32r2/o32 translation of the ALU semantics in ../step.c.
 * No code from an external emulator is used here. */
#include "emit.h"
#include "gba.h"
#include <string.h>

enum { Z=0, A=4, N=8, M=9, D=10, P=11, C=12, V=13, X=14, Y=15, Q=24, RA=31 };
struct Emit { uint32_t words[HC_MIPS_WORDS]; size_t n; };
static void r(struct Emit* e, unsigned fn, unsigned d, unsigned s, unsigned t) {
    e->words[e->n++] = (s<<21)|(t<<16)|(d<<11)|fn;
}
static void i(struct Emit* e, unsigned op, unsigned t, unsigned s, uint16_t imm) {
    e->words[e->n++] = (op<<26)|(s<<21)|(t<<16)|imm;
}
static void shift(struct Emit* e, unsigned fn, unsigned d, unsigned t, unsigned sh) {
    e->words[e->n++] = (t<<16)|(d<<11)|(sh<<6)|fn;
}
static void li(struct Emit* e, unsigned d, uint32_t value) {
    i(e,15,d,Z,value>>16);
    if (value & 0xffff) i(e,13,d,d,value);
}
static void carryIn(struct Emit* e, unsigned d) {
    shift(e,2,d,P,29);
    i(e,12,d,d,1);
}
static void bit(struct Emit* e, unsigned d, unsigned s, unsigned b) {
    shift(e,2,d,s,b);
    i(e,12,d,d,1);
}

size_t hcMipsEmit(uint32_t* code, size_t capacity, const struct Inst* in) {
    // Register shifts have a bus-idle cycle. PC writes can change mode/refill
    // the pipeline. Keep both in stepExecute until their timing is translated.
    if (in->kind != I_ALU || in->rd == 15 ||
        (!in->op2.isImm && in->op2.byReg) || in->op > ALU_MVN) return 0;
    struct Emit e = {{0},0};
    i(&e,35,P,A,offsetof(struct CPU,cpsr)); // lw
    i(&e,35,N,A,offsetof(struct CPU,r) + 4*in->rn);
    carryIn(&e,C);
    if (in->op2.isImm) {
        li(&e,M,in->op2.imm);
        if (in->op2.immRotated) li(&e,C,in->op2.imm>>31);
    } else {
        unsigned a = in->op2.amount;
        i(&e,35,M,A,offsetof(struct CPU,r) + 4*in->op2.rm);
        switch (in->op2.type) {
        case SHIFT_LSL:
            if (a) { bit(&e,C,M,32-a); shift(&e,0,M,M,a); }
            break;
        case SHIFT_LSR:
            if (a) { bit(&e,C,M,a-1); shift(&e,2,M,M,a); }
            else { shift(&e,2,C,M,31); r(&e,0x21,M,Z,Z); }
            break;
        case SHIFT_ASR:
            bit(&e,C,M,a ? a-1 : 31);
            shift(&e,3,M,M,a ? a : 31);
            break;
        case SHIFT_ROR:
            if (a) {
                bit(&e,C,M,a-1);
                // rotr: SPECIAL, rs=1, funct=SRL.
                e.words[e.n++] = (1u<<21)|(M<<16)|(M<<11)|(a<<6)|2;
            } else {
                bit(&e,C,M,0);
                carryIn(&e,Y);
                shift(&e,0,Y,Y,31);
                shift(&e,2,M,M,1);
                r(&e,0x25,M,M,Y);
            }
            break;
        default: return 0;
        }
    }
    bool arithmetic = false, add = false;
    unsigned left=N, right=M;
    switch (in->op) {
    case ALU_AND: case ALU_TST: r(&e,0x24,D,N,M); break;
    case ALU_EOR: case ALU_TEQ: r(&e,0x26,D,N,M); break;
    case ALU_ORR: r(&e,0x25,D,N,M); break;
    case ALU_MOV: r(&e,0x21,D,M,Z); break;
    case ALU_BIC: r(&e,0x27,X,M,Z); r(&e,0x24,D,N,X); break;
    case ALU_MVN: r(&e,0x27,D,M,Z); break;
    default:
        arithmetic = true;
        add = in->op == ALU_ADD || in->op == ALU_CMN || in->op == ALU_ADC;
        if (in->op == ALU_RSB || in->op == ALU_RSC) { left=M; right=N; }
        if (add) {
            r(&e,0x21,Q,left,right); // addu; signed overflow must never trap
            r(&e,0x2b,C,Q,left);     // unsigned carry
            if (in->op == ALU_ADC) {
                carryIn(&e,Y);
                r(&e,0x21,D,Q,Y);
                r(&e,0x2b,X,D,Q);
                r(&e,0x25,C,C,X);
            } else r(&e,0x21,D,Q,Z);
        } else {
            r(&e,0x23,Q,left,right); // subu
            r(&e,0x2b,C,left,right); // borrow
            if (in->op == ALU_SBC || in->op == ALU_RSC) {
                carryIn(&e,Y);
                i(&e,14,Y,Y,1);
                r(&e,0x23,D,Q,Y);
                r(&e,0x2b,X,Q,Y);
                r(&e,0x25,C,C,X);
            } else r(&e,0x21,D,Q,Z);
            i(&e,14,C,C,1); // ARM C is no-borrow
        }
        if (in->setFlags) {
            r(&e,0x26,X,left,right);
            if (add) r(&e,0x27,X,X,Z);
            r(&e,0x26,Y,left,D);
            r(&e,0x24,V,X,Y);
            shift(&e,2,V,V,31);
        }
        break;
    }
    if (!(in->op >= ALU_TST && in->op <= ALU_CMN))
        i(&e,43,D,A,offsetof(struct CPU,r)+4*in->rd);
    if (in->setFlags) {
        // Preserve low CPSR bits and, for logical operations, the old V.
        li(&e,X,arithmetic ? 0x0fffffff : 0x1fffffff);
        r(&e,0x24,P,P,X);
        li(&e,X,PSR_N);
        r(&e,0x24,X,D,X);
        r(&e,0x25,P,P,X);
        i(&e,11,X,D,1); // sltiu X,D,1 => D==0
        shift(&e,0,X,X,30);
        r(&e,0x25,P,P,X);
        shift(&e,0,C,C,29);
        r(&e,0x25,P,P,C);
        if (arithmetic) {
            shift(&e,0,V,V,28);
            r(&e,0x25,P,P,V);
        }
        i(&e,43,P,A,offsetof(struct CPU,cpsr));
    }
    r(&e,8,Z,RA,Z); // jr ra
    e.words[e.n++] = 0; // architectural delay slot
    if (e.n > capacity || e.n > HC_MIPS_WORDS) return 0;
    memcpy(code,e.words,e.n*sizeof(uint32_t));
    return e.n;
}
