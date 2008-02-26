// 16bit code to access hard drives.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "disk.h" // floppy_13
#include "biosvar.h" // struct bregs
#include "util.h" // debug_enter

static void
disk_13(struct bregs *regs, u8 drive)
{
    set_cf(regs, 1);
}

static void
handle_legacy_disk(struct bregs *regs, u8 drive)
{
    if (drive < 0x80) {
        floppy_13(regs, drive);
        return;
    }
#if BX_USE_ATADRV
    if (drive >= 0xE0) {
        int13_cdrom(regs); // xxx
        return;
    }
#endif

    disk_13(regs, drive);
}

void VISIBLE
handle_40(struct bregs *regs)
{
    debug_enter(regs);
    handle_legacy_disk(regs, regs->dl);
    debug_exit(regs);
}

// INT 13h Fixed Disk Services Entry Point
void VISIBLE
handle_13(struct bregs *regs)
{
    //debug_enter(regs);
    u8 drive = regs->dl;
#if BX_ELTORITO_BOOT
    if (regs->ah >= 0x4a || regs->ah <= 0x4d) {
        int13_eltorito(regs);
    } else if (cdemu_isactive() && cdrom_emulated_drive()) {
        int13_cdemu(regs);
    } else
#endif
        handle_legacy_disk(regs, drive);
    debug_exit(regs);
}

// record completion in BIOS task complete flag
void VISIBLE
handle_76(struct bregs *regs)
{
    debug_enter(regs);
    SET_BDA(floppy_harddisk_info, 0xff);
    eoi_both_pics();
}
