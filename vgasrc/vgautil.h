// Misc function and variable declarations.
#ifndef __VGAUTIL_H
#define __VGAUTIL_H

#include "types.h" // u8

// swcursor.c
struct bregs;
void swcursor_pre_handle10(struct bregs *regs);
void swcursor_check_event(void);

// vbe.c
extern u32 VBE_total_memory;
extern u32 VBE_capabilities;
extern u32 VBE_framebuffer;
extern u16 VBE_win_granularity;
void handle_104f(struct bregs *regs);

// vgafonts.c
extern u8 vgafont8[];
extern u8 vgafont14[];
extern u8 vgafont16[];
extern u8 vgafont14alt[];
extern u8 vgafont16alt[];

// vgainit.c
extern int VgaBDF;
extern int HaveRunInit;

#endif // vgautil.h
