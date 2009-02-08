// Code to load disk image and start system boot.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // irq_enable
#include "biosvar.h" // GET_EBDA
#include "config.h" // CONFIG_*
#include "disk.h" // cdrom_boot
#include "bregs.h" // struct bregs
#include "boot.h" // struct ipl_s
#include "cmos.h" // inb_cmos

struct ipl_s IPL;


/****************************************************************
 * IPL handlers
 ****************************************************************/

void
boot_setup()
{
    if (! CONFIG_BOOT)
        return;
    dprintf(3, "init boot device ordering\n");

    memset(&IPL, 0, sizeof(IPL));

    // Floppy drive
    struct ipl_entry_s *ie = &IPL.table[0];
    ie->type = IPL_TYPE_FLOPPY;
    ie->description = "Floppy";
    ie++;

    // First HDD
    ie->type = IPL_TYPE_HARDDISK;
    ie->description = "Hard Disk";
    ie++;

    // CDROM
    if (CONFIG_CDROM_BOOT) {
        ie->type = IPL_TYPE_CDROM;
        ie->description = "CD-Rom";
        ie++;
    }

    IPL.count = ie - IPL.table;
    SET_EBDA(boot_sequence, 0xffff);
    if (CONFIG_COREBOOT) {
        // XXX - hardcode defaults for coreboot.
        IPL.bootorder = 0x00000231;
        IPL.checkfloppysig = 1;
    } else {
        // On emulators, get boot order from nvram.
        IPL.bootorder = (inb_cmos(CMOS_BIOS_BOOTFLAG2)
                         | ((inb_cmos(CMOS_BIOS_BOOTFLAG1) & 0xf0) << 4));
        if (!(inb_cmos(CMOS_BIOS_BOOTFLAG1) & 1))
            IPL.checkfloppysig = 1;
    }
}

// Add a BEV vector for a given pnp compatible option rom.
void
add_bev(u16 seg, u16 bev, u16 desc)
{
    // Found a device that thinks it can boot the system.  Record
    // its BEV and product name string.

    if (! CONFIG_BOOT)
        return;

    if (IPL.count >= ARRAY_SIZE(IPL.table))
        return;

    struct ipl_entry_s *ie = &IPL.table[IPL.count];
    ie->type = IPL_TYPE_BEV;
    ie->vector = (seg << 16) | bev;
    if (desc)
        ie->description = MAKE_FLATPTR(seg, desc);

    IPL.count++;
}


/****************************************************************
 * Boot menu
 ****************************************************************/

void
interactive_bootmenu()
{
    if (! CONFIG_BOOTMENU)
        return;

    while (get_keystroke(0) >= 0)
        ;

    printf("Press F12 for boot menu.\n\n");

    int scan_code = get_keystroke(2500);
    if (scan_code != 0x86)
        /* not F12 */
        return;

    while (get_keystroke(0) >= 0)
        ;

    printf("Select boot device:\n\n");

    int count = IPL.count;
    int i;
    for (i = 0; i < count; i++) {
        struct ipl_entry_s *ie = &IPL.table[i];
        char desc[33];
        printf("%d. %s\n", i+1
               , strtcpy(desc, ie->description, ARRAY_SIZE(desc)));
    }

    for (;;) {
        scan_code = get_keystroke(1000);
        if (scan_code == 0x01)
            // ESC
            break;
        if (scan_code >= 0 && scan_code <= count + 1) {
            // Add user choice to the boot order.
            u16 choice = scan_code - 1;
            u32 bootorder = IPL.bootorder;
            IPL.bootorder = (bootorder << 4) | choice;
            break;
        }
    }
    printf("\n");
}


/****************************************************************
 * Boot code (int 18/19)
 ****************************************************************/

static void
call_boot_entry(u16 bootseg, u16 bootip, u8 bootdrv)
{
    dprintf(1, "Booting from %x:%x\n", bootseg, bootip);

    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ip = bootip;
    br.cs = bootseg;
    // Set the magic number in ax and the boot drive in dl.
    br.dl = bootdrv;
    br.ax = 0xaa55;
    call16(&br);
}

// Boot from a disk (either floppy or harddrive)
static void
boot_disk(u8 bootdrv, int checksig)
{
    u16 bootseg = 0x07c0;

    // Read sector
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.dl = bootdrv;
    br.es = bootseg;
    br.ah = 2;
    br.al = 1;
    br.cl = 1;
    call16_int(0x13, &br);

    if (br.flags & F_CF) {
        printf("Boot failed: could not read the boot disk\n\n");
        return;
    }

    if (checksig) {
        struct mbr_s *mbr = (void*)0;
        if (GET_FARVAR(bootseg, mbr->signature) != MBR_SIGNATURE) {
            printf("Boot failed: not a bootable disk\n\n");
            return;
        }
    }

    /* Canonicalize bootseg:bootip */
    u16 bootip = (bootseg & 0x0fff) << 4;
    bootseg &= 0xf000;

    call_boot_entry(bootseg, bootip, bootdrv);
}

// Boot from a CD-ROM
static void
boot_cdrom()
{
    if (! CONFIG_CDROM_BOOT)
        return;
    int status = cdrom_boot();
    if (status) {
        printf("Boot failed: Could not read from CDROM (code %04x)\n", status);
        return;
    }

    u16 ebda_seg = get_ebda_seg();
    u8 bootdrv = GET_EBDA2(ebda_seg, cdemu.emulated_drive);
    u16 bootseg = GET_EBDA2(ebda_seg, cdemu.load_segment);
    /* Canonicalize bootseg:bootip */
    u16 bootip = (bootseg & 0x0fff) << 4;
    bootseg &= 0xf000;

    call_boot_entry(bootseg, bootip, bootdrv);
}

static void
do_boot(u16 seq_nr)
{
    if (! CONFIG_BOOT)
        panic("Boot support not compiled in.\n");

    u32 bootdev = IPL.bootorder;
    bootdev >>= 4 * seq_nr;
    bootdev &= 0xf;

    if (bootdev == 0)
        panic("No bootable device.\n");

    /* Translate bootdev to an IPL table offset by subtracting 1 */
    bootdev -= 1;

    if (bootdev >= IPL.count) {
        dprintf(1, "Invalid boot device (0x%x)\n", bootdev);
        goto fail;
    }

    /* Do the loading, and set up vector as a far pointer to the boot
     * address, and bootdrv as the boot drive */
    struct ipl_entry_s *ie = &IPL.table[bootdev];
    char desc[33];
    printf("Booting from %s...\n"
           , strtcpy(desc, ie->description, ARRAY_SIZE(desc)));

    switch(ie->type) {
    case IPL_TYPE_FLOPPY:
        boot_disk(0x00, IPL.checkfloppysig);
        break;
    case IPL_TYPE_HARDDISK:
        boot_disk(0x80, 1);
        break;
    case IPL_TYPE_CDROM:
        boot_cdrom();
        break;
    case IPL_TYPE_BEV:
        call_boot_entry(ie->vector >> 16, ie->vector & 0xffff, 0);
        break;
    }

    // Boot failed: invoke the boot recovery function
    struct bregs br;
fail:
    memset(&br, 0, sizeof(br));
    call16_int(0x18, &br);
}

// Boot Failure recovery: try the next device.
void VISIBLE32
handle_18()
{
    debug_serial_setup();
    debug_enter(NULL, DEBUG_HDL_18);
    u16 ebda_seg = get_ebda_seg();
    u16 seq = GET_EBDA2(ebda_seg, boot_sequence) + 1;
    SET_EBDA2(ebda_seg, boot_sequence, seq);
    do_boot(seq);
}

// INT 19h Boot Load Service Entry Point
void VISIBLE32
handle_19()
{
    debug_serial_setup();
    debug_enter(NULL, DEBUG_HDL_19);
    SET_EBDA(boot_sequence, 0);
    do_boot(0);
}

// Ughh - some older gcc compilers have a bug which causes VISIBLE32
// functions to not be exported as global variables.
asm(".global handle_18, handle_19");
