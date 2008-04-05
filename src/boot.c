// 16bit code to load disk image and start system boot.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "util.h" // irq_enable
#include "biosvar.h" // struct bregs
#include "config.h" // CONFIG_*
#include "cmos.h" // inb_cmos
#include "ata.h" // ata_detect
#include "disk.h" // cdrom_boot

// We need a copy of this string, but we are not actually a PnP BIOS,
// so make sure it is *not* aligned, so OSes will not see it if they
// scan.
char pnp_string[] VISIBLE16 __attribute__((aligned (2))) = " $PnP";

//--------------------------------------------------------------------------
// print_boot_device
//   displays the boot device
//--------------------------------------------------------------------------

static const char drivetypes[][10]={
    "", "Floppy","Hard Disk","CD-Rom", "Network"
};

void
printf_bootdev(u16 bootdev)
{
    u16 type = GET_EBDA(ipl.table[bootdev].type);

    /* NIC appears as type 0x80 */
    if (type == IPL_TYPE_BEV)
        type = 0x4;
    if (type == 0 || type > 0x4)
        BX_PANIC("Bad drive type\n");
    printf("%s", drivetypes[type]);

    /* print product string if BEV */
    void *far_description = (void*)GET_EBDA(ipl.table[bootdev].description);
    if (type == 4 && far_description != 0) {
        char description[33];
        /* first 32 bytes are significant */
        memcpy(MAKE_FARPTR(GET_SEG(SS), &description), far_description, 32);
        /* terminate string */
        description[32] = 0;
        printf(" [%.s]", description);
    }
}

static void
print_boot_device(u16 bootdev)
{
    printf("Booting from ");
    printf_bootdev(bootdev);
    printf("...\n");
}

//--------------------------------------------------------------------------
// print_boot_failure
//   displays the reason why boot failed
//--------------------------------------------------------------------------
static void
print_boot_failure(u16 type, u8 reason)
{
    if (type == 0 || type > 0x3)
        BX_PANIC("Bad drive type\n");

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

static void
try_boot(u16 seq_nr)
{
    irq_enable();

    SET_EBDA(ipl.sequence, seq_nr);

    u16 bootdev = inb_cmos(CMOS_BIOS_BOOTFLAG2);
    bootdev |= ((inb_cmos(CMOS_BIOS_BOOTFLAG1) & 0xf0) << 4);
    bootdev >>= 4 * seq_nr;
    bootdev &= 0xf;

    /* Read user selected device */
    u16 bootfirst = GET_EBDA(ipl.bootfirst);
    if (bootfirst != 0xFFFF) {
        bootdev = bootfirst;
        /* Reset boot sequence */
        SET_EBDA(ipl.bootfirst, 0xFFFF);
        SET_EBDA(ipl.sequence, 0xFFFF);
    }

    if (bootdev == 0)
        BX_PANIC("No bootable device.\n");

    /* Translate from CMOS runes to an IPL table offset by subtracting 1 */
    bootdev -= 1;

    if (bootdev >= GET_EBDA(ipl.count)) {
        BX_INFO("Invalid boot device (0x%x)\n", bootdev);
        return;
    }

    /* Do the loading, and set up vector as a far pointer to the boot
     * address, and bootdrv as the boot drive */
    print_boot_device(bootdev);

    u16 type = GET_EBDA(ipl.table[bootdev].type);

    u16 bootseg, bootip;
    u8 bootdrv = 0;
    struct bregs cr;
    switch(type) {
    case IPL_TYPE_FLOPPY: /* FDD */
    case IPL_TYPE_HARDDISK: /* HDD */

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
         * only do the check if the CMOS doesn't tell us to skip it */
        if ((type != IPL_TYPE_FLOPPY)
            || !((inb_cmos(CMOS_BIOS_BOOTFLAG1) & 0x01))) {
            if (GET_FARVAR(bootseg, *(u16*)0x1fe) != 0xaa55) {
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
            break;
        u16 status = cdrom_boot();
        if (status) {
            printf("CDROM boot failure code : %04x\n", status);
            print_boot_failure(type, 1);
            return;
        }

        bootdrv = GET_EBDA(cdemu.emulated_drive);
        bootseg = GET_EBDA(cdemu.load_segment);
        /* Canonicalize bootseg:bootip */
        bootip = (bootseg & 0x0fff) << 4;
        bootseg &= 0xf000;
        break;
    }
    case IPL_TYPE_BEV: {
        /* Expansion ROM with a Bootstrap Entry Vector (a far
         * pointer) */
        u32 vector = GET_EBDA(ipl.table[bootdev].vector);
        bootseg = vector >> 16;
        bootip = vector & 0xffff;
        break;
    }
    default:
        return;
    }

    /* Debugging info */
    BX_INFO("Booting from %x:%x\n", bootseg, bootip);

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
void VISIBLE16
handle_18()
{
    debug_enter(NULL);
    u16 seq = GET_EBDA(ipl.sequence) + 1;
    do_boot(seq);
}

// INT 19h Boot Load Service Entry Point
void VISIBLE16
handle_19()
{
    debug_enter(NULL);
    do_boot(0);
}
