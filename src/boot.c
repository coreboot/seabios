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
    struct ipl_entry_s *ip = &IPL.table[0];
    ip->type = IPL_TYPE_FLOPPY;
    ip++;

    // First HDD
    ip->type = IPL_TYPE_HARDDISK;
    ip++;

    // CDROM
    if (CONFIG_CDROM_BOOT) {
        ip->type = IPL_TYPE_CDROM;
        ip++;
    }

    IPL.count = ip - IPL.table;
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

    struct ipl_entry_s *ip = &IPL.table[IPL.count];
    ip->type = IPL_TYPE_BEV;
    ip->vector = (seg << 16) | bev;
    if (desc)
        ip->description = MAKE_FLATPTR(seg, desc);

    IPL.count++;
}


/****************************************************************
 * Printing helpers
 ****************************************************************/

static const char drivetypes[][10]={
    "", "Floppy", "Hard Disk", "CD-Rom", "Network"
};

// display a device name
static void
printf_bootdev(u16 bootdev)
{
    u16 type = IPL.table[bootdev].type;

    /* NIC appears as type 0x80 */
    if (type == IPL_TYPE_BEV)
        type = 0x4;
    if (type == 0 || type > 0x4) {
        printf("Unknown");
        return;
    }
    printf("%s", drivetypes[type]);

    /* print product string if BEV */
    char *description_fl = IPL.table[bootdev].description;
    if (type == 4 && description_fl != 0) {
        char description[33];
        /* first 32 bytes are significant */
        memcpy(description, description_fl, 32);
        /* terminate string */
        description[32] = 0;
        printf(" [%.s]", description);
    }
}

// display the boot device
static void
print_boot_device(u16 bootdev)
{
    printf("Booting from ");
    printf_bootdev(bootdev);
    printf("...\n");
}

// display the reason why a boot failed
static void
print_boot_failure(u16 type, u8 reason)
{
    printf("Boot failed");
    if (type < 4) {
        /* Report the reason too */
        if (reason==0)
            printf(": not a bootable disk");
        else
            printf(": could not read the boot disk");
    }
    printf("\n\n");
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
        printf("%d. ", i+1);
        printf_bootdev(i);
        printf("\n");
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
try_boot(u16 seq_nr)
{
    if (! CONFIG_BOOT)
        BX_PANIC("Boot support not compiled in.\n");

    u32 bootdev = IPL.bootorder;
    bootdev >>= 4 * seq_nr;
    bootdev &= 0xf;

    if (bootdev == 0)
        BX_PANIC("No bootable device.\n");

    /* Translate bootdev to an IPL table offset by subtracting 1 */
    bootdev -= 1;

    if (bootdev >= IPL.count) {
        dprintf(1, "Invalid boot device (0x%x)\n", bootdev);
        return;
    }

    /* Do the loading, and set up vector as a far pointer to the boot
     * address, and bootdrv as the boot drive */
    print_boot_device(bootdev);

    u16 type = IPL.table[bootdev].type;

    u16 bootseg, bootip;
    u8 bootdrv = 0;
    struct bregs cr;
    switch(type) {
    case IPL_TYPE_FLOPPY:
    case IPL_TYPE_HARDDISK:

        bootdrv = (type == IPL_TYPE_HARDDISK) ? 0x80 : 0x00;
        bootseg = 0x07c0;

        // Read sector
        memset(&cr, 0, sizeof(cr));
        cr.dl = bootdrv;
        cr.es = bootseg;
        cr.ah = 2;
        cr.al = 1;
        cr.cl = 1;
        call16_int(0x13, &cr);

        if (cr.flags & F_CF) {
            print_boot_failure(type, 1);
            return;
        }

        /* Always check the signature on a HDD boot sector; on FDD,
         * only do the check if configured for it */
        if (type != IPL_TYPE_FLOPPY || IPL.checkfloppysig) {
            struct mbr_s *mbr = (void*)0;
            if (GET_FARVAR(bootseg, mbr->signature) != MBR_SIGNATURE) {
                print_boot_failure(type, 0);
                return;
            }
        }

        /* Canonicalize bootseg:bootip */
        bootip = (bootseg & 0x0fff) << 4;
        bootseg &= 0xf000;
        break;
    case IPL_TYPE_CDROM: {
        /* CD-ROM */
        if (! CONFIG_CDROM_BOOT)
            return;
        int status = cdrom_boot();
        if (status) {
            printf("CDROM boot failure code : %04x\n", status);
            print_boot_failure(type, 1);
            return;
        }

        u16 ebda_seg = get_ebda_seg();
        bootdrv = GET_EBDA2(ebda_seg, cdemu.emulated_drive);
        bootseg = GET_EBDA2(ebda_seg, cdemu.load_segment);
        /* Canonicalize bootseg:bootip */
        bootip = (bootseg & 0x0fff) << 4;
        bootseg &= 0xf000;
        break;
    }
    case IPL_TYPE_BEV: {
        /* Expansion ROM with a Bootstrap Entry Vector (a far
         * pointer) */
        u32 vector = IPL.table[bootdev].vector;
        bootseg = vector >> 16;
        bootip = vector & 0xffff;
        break;
    }
    default:
        return;
    }

    /* Debugging info */
    dprintf(1, "Booting from %x:%x\n", bootseg, bootip);

    memset(&cr, 0, sizeof(cr));
    cr.ip = bootip;
    cr.cs = bootseg;
    // Set the magic number in ax and the boot drive in dl.
    cr.dl = bootdrv;
    cr.ax = 0xaa55;
    call16(&cr);
}

static void
do_boot(u16 seq_nr)
{
    try_boot(seq_nr);

    // Boot failed: invoke the boot recovery function
    struct bregs br;
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
