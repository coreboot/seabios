// Structure layout of cpu registers the the bios uses.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#ifndef __BREGS_H
#define __BREGS_H


/****************************************************************
 * Registers saved/restored in romlayout.S
 ****************************************************************/

#define UREG(ER, R, RH, RL) union { u32 ER; struct { u16 R; u16 R ## _hi; }; struct { u8 RL; u8 RH; u8 R ## _hilo; u8 R ## _hihi; }; }

// Layout of registers passed in to irq handlers.  Note that this
// layout corresponds to code in romlayout.S - don't change it here
// without also updating the assembler code.
struct bregs {
    u16 ds;
    u16 es;
    UREG(edi, di, di_hi, di_lo);
    UREG(esi, si, si_hi, si_lo);
    UREG(ebx, bx, bh, bl);
    UREG(edx, dx, dh, dl);
    UREG(ecx, cx, ch, cl);
    UREG(eax, ax, ah, al);
    u16 ip;
    u16 cs;
    u16 flags;
} PACKED;


/****************************************************************
 * Helper functions
 ****************************************************************/

// bregs flags bitdefs
#define F_ZF (1<<6)
#define F_CF (1<<0)

static inline void
set_cf(struct bregs *regs, int cond)
{
    if (cond)
        regs->flags |= F_CF;
    else
        regs->flags &= ~F_CF;
}

// Frequently used return codes
#define RET_EUNSUPPORTED 0x86

static inline void
set_success(struct bregs *regs)
{
    set_cf(regs, 0);
}

static inline void
set_code_success(struct bregs *regs)
{
    regs->ah = 0;
    set_cf(regs, 0);
}

static inline void
set_fail_silent(struct bregs *regs)
{
    set_cf(regs, 1);
}

static inline void
set_code_fail_silent(struct bregs *regs, u8 code)
{
    regs->ah = code;
    set_cf(regs, 1);
}

#define set_fail(regs) \
    __set_fail(__func__, (regs))
#define set_code_fail(regs, code)               \
    __set_code_fail(__func__, (regs), (code))

// util.c
void __set_fail(const char *fname, struct bregs *regs);
void __set_code_fail(const char *fname, struct bregs *regs, u8 code);

#endif // bregs.h
