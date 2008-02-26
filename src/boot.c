// 16bit code to load disk image and start system boot.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "types.h" // VISIBLE
#include "util.h" // irq_enable
#include "biosvar.h" // struct bregs
#include "farptr.h" // SET_SEG

static inline void
__call_irq(u8 nr)
{
    asm volatile("int %0" : : "N" (nr));
}

static inline u32
call_irq(u8 nr, struct bregs *callregs)
{
    u32 flags;
    asm volatile(
        // Save current registers
        "pushal\n"
        // Pull in calling registers.
        "movl 0x04(%%eax), %%edi\n"
        "movl 0x08(%%eax), %%esi\n"
        "movl 0x0c(%%eax), %%ebp\n"
        "movl 0x14(%%eax), %%ebx\n"
        "movl 0x18(%%eax), %%edx\n"
        "movl 0x1c(%%eax), %%ecx\n"
        "movl 0x20(%%eax), %%eax\n"
        // Invoke interrupt
        "int %1\n"
        // Restore registers
        "popal\n"
        // Exract flags
        "pushfw\n"
        "popl %%eax\n"
        : "=a" (flags): "N" (nr), "a" (callregs), "m" (*callregs));
    return flags;
}

static void
print_boot_failure()
{
    bprintf(0, "Boot failed\n");
}

static void
try_boot()
{
    // XXX - assume floppy
    u16 bootseg = 0x07c0;
    u8 bootdrv = 0;

    // Read sector
    struct bregs cr;
    memset(&cr, 0, sizeof(cr));
    cr.dl = bootdrv;
    SET_SEG(ES, bootseg);
    cr.bx = 0;
    cr.ah = 2;
    cr.al = 1;
    cr.ch = 0;
    cr.cl = 1;
    cr.dh = 0;
    u32 status = call_irq(0x13, &cr);

    if (status & F_CF) {
        print_boot_failure();
        return;
    }

    u16 bootip = (bootseg & 0x0fff) << 4;
    bootseg &= 0xf000;

    u32 segoff = (bootseg << 16) | bootip;
    asm volatile (
        "pushf\n"
        "pushl %0\n"
        "movb %b1, %%dl\n"
        // Set the magic number in ax and the boot drive in dl.
        "movw $0xaa55, %%ax\n"
        // Zero some of the other registers.
        "xorw %%bx, %%bx\n"
        "movw %%bx, %%ds\n"
        "movw %%bx, %%es\n"
        "movw %%bx, %%bp\n"
        // Go!
        "iretw\n"
        : : "r" (segoff), "ri" (bootdrv));
}

// Boot Failure recovery: try the next device.
void VISIBLE
handle_18(struct bregs *regs)
{
    debug_enter(regs);
    try_boot();
}

// INT 19h Boot Load Service Entry Point
void VISIBLE
handle_19(struct bregs *regs)
{
    debug_enter(regs);
    try_boot();
}

// Callback from 32bit entry - start boot process
void VISIBLE
begin_boot()
{
    irq_enable();
    __call_irq(0x19);
}
