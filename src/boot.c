// Code to load disk image and start system boot.
//
// Copyright (C) 2008-2010  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "biosvar.h" // GET_EBDA
#include "config.h" // CONFIG_*
#include "disk.h" // cdrom_boot
#include "bregs.h" // struct bregs
#include "boot.h" // struct ipl_s
#include "cmos.h" // inb_cmos
#include "paravirt.h"

struct ipl_s IPL;


/****************************************************************
 * IPL and BCV handlers
 ****************************************************************/

void
boot_setup(void)
{
    if (! CONFIG_BOOT)
        return;
    dprintf(3, "init boot device ordering\n");

    memset(&IPL, 0, sizeof(IPL));
    struct ipl_entry_s *ie = &IPL.bev[0];

    // Floppy drive
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
        ie->description = "DVD/CD";
        ie++;
    }

    if (CONFIG_COREBOOT && CONFIG_COREBOOT_FLASH) {
        ie->type = IPL_TYPE_CBFS;
        ie->description = "CBFS";
        ie++;
    }

    IPL.bevcount = ie - IPL.bev;
    SET_EBDA(boot_sequence, 0xffff);
    if (CONFIG_COREBOOT) {
        // XXX - hardcode defaults for coreboot.
        IPL.bootorder = 0x87654231;
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
    if (! CONFIG_BOOT)
        return;
    if (IPL.bevcount >= ARRAY_SIZE(IPL.bev))
        return;

    struct ipl_entry_s *ie = &IPL.bev[IPL.bevcount++];
    ie->type = IPL_TYPE_BEV;
    ie->vector = (seg << 16) | bev;
    const char *d = "Unknown";
    if (desc)
        d = MAKE_FLATPTR(seg, desc);
    ie->description = d;
}

// Add a bcv entry for an expansion card harddrive or legacy option rom
void
add_bcv(u16 seg, u16 ip, u16 desc)
{
    if (! CONFIG_BOOT)
        return;
    if (IPL.bcvcount >= ARRAY_SIZE(IPL.bcv))
        return;

    struct ipl_entry_s *ie = &IPL.bcv[IPL.bcvcount++];
    ie->type = BCV_TYPE_EXTERNAL;
    ie->vector = (seg << 16) | ip;
    const char *d = "Legacy option rom";
    if (desc)
        d = MAKE_FLATPTR(seg, desc);
    ie->description = d;
}

// Add a bcv entry for an internal harddrive
void
add_bcv_internal(struct drive_s *drive_g)
{
    if (! CONFIG_BOOT)
        return;
    if (IPL.bcvcount >= ARRAY_SIZE(IPL.bcv))
        return;

    struct ipl_entry_s *ie = &IPL.bcv[IPL.bcvcount++];
    if (CONFIG_THREADS) {
        // Add to bcv list with assured drive order.
        struct ipl_entry_s *end = ie;
        for (;;) {
            struct ipl_entry_s *prev = ie - 1;
            if (prev < IPL.bcv || prev->type != BCV_TYPE_INTERNAL)
                break;
            struct drive_s *prevdrive = (void*)prev->vector;
            if (prevdrive->type < drive_g->type
                || (prevdrive->type == drive_g->type
                    && prevdrive->cntl_id < drive_g->cntl_id))
                break;
            ie--;
        }
        if (ie != end)
            memmove(ie+1, ie, (void*)end-(void*)ie);
    }
    ie->type = BCV_TYPE_INTERNAL;
    ie->vector = (u32)drive_g;
    ie->description = "";
}


/****************************************************************
 * Boot menu and BCV execution
 ****************************************************************/

// Show a generic menu item
static int
menu_show_default(struct ipl_entry_s *ie, int menupos)
{
    char desc[33];
    printf("%d. %s\n", menupos
           , strtcpy(desc, ie->description, ARRAY_SIZE(desc)));
    return 1;
}

// Show floppy menu item - but only if there exists a floppy drive.
static int
menu_show_floppy(struct ipl_entry_s *ie, int menupos)
{
    int i;
    for (i = 0; i < Drives.floppycount; i++) {
        struct drive_s *drive_g = getDrive(EXTTYPE_FLOPPY, i);
        printf("%d. Floppy [%s]\n", menupos + i, drive_g->desc);
    }
    return Drives.floppycount;
}

// Show menu items from BCV list.
static int
menu_show_harddisk(struct ipl_entry_s *ie, int menupos)
{
    int i;
    for (i = 0; i < IPL.bcvcount; i++) {
        struct ipl_entry_s *ie = &IPL.bcv[i];
        struct drive_s *drive_g = (void*)ie->vector;
        switch (ie->type) {
        case BCV_TYPE_INTERNAL:
            printf("%d. %s\n", menupos + i, drive_g->desc);
            break;
        default:
            menu_show_default(ie, menupos+i);
            break;
        }
    }
    return IPL.bcvcount;
}

// Show cdrom menu item - but only if there exists a cdrom drive.
static int
menu_show_cdrom(struct ipl_entry_s *ie, int menupos)
{
    int i;
    for (i = 0; i < Drives.cdcount; i++) {
        struct drive_s *drive_g = getDrive(EXTTYPE_CD, i);
        printf("%d. DVD/CD [%s]\n", menupos + i, drive_g->desc);
    }
    return Drives.cdcount;
}

// Show coreboot-fs menu item.
static int
menu_show_cbfs(struct ipl_entry_s *ie, int menupos)
{
    int count = 0;
    struct cbfs_file *file = NULL;
    for (;;) {
        file = cbfs_findprefix("img/", file);
        if (!file)
            break;
        const char *filename = cbfs_filename(file);
        printf("%d. Payload [%s]\n", menupos + count, &filename[4]);
        count++;
        if (count > 8)
            break;
    }
    return count;
}

// Show IPL option menu.
static void
interactive_bootmenu(void)
{
    if (! CONFIG_BOOTMENU || ! qemu_cfg_show_boot_menu())
        return;

    while (get_keystroke(0) >= 0)
        ;

    printf("Press F12 for boot menu.\n\n");

    enable_bootsplash();
    int scan_code = get_keystroke(CONFIG_BOOTMENU_WAIT);
    disable_bootsplash();
    if (scan_code != 0x86)
        /* not F12 */
        return;

    while (get_keystroke(0) >= 0)
        ;

    printf("Select boot device:\n\n");
    wait_threads();

    int subcount[ARRAY_SIZE(IPL.bev)];
    int menupos = 1;
    int i;
    for (i = 0; i < IPL.bevcount; i++) {
        struct ipl_entry_s *ie = &IPL.bev[i];
        int sc;
        switch (ie->type) {
        case IPL_TYPE_FLOPPY:
            sc = menu_show_floppy(ie, menupos);
            break;
        case IPL_TYPE_HARDDISK:
            sc = menu_show_harddisk(ie, menupos);
            break;
        case IPL_TYPE_CDROM:
            sc = menu_show_cdrom(ie, menupos);
            break;
        case IPL_TYPE_CBFS:
            sc = menu_show_cbfs(ie, menupos);
            break;
        default:
            sc = menu_show_default(ie, menupos);
            break;
        }
        subcount[i] = sc;
        menupos += sc;
    }

    for (;;) {
        scan_code = get_keystroke(1000);
        if (scan_code == 0x01)
            // ESC
            break;
        if (scan_code < 1 || scan_code > menupos)
            continue;
        int choice = scan_code - 1;

        // Find out which IPL this was for.
        int bev = 0;
        while (choice > subcount[bev]) {
            choice -= subcount[bev];
            bev++;
        }
        IPL.bev[bev].subchoice = choice-1;

        // Add user choice to the boot order.
        IPL.bootorder = (IPL.bootorder << 4) | (bev+1);
        break;
    }
    printf("\n");
}

// Run the specified bcv.
static void
run_bcv(struct ipl_entry_s *ie)
{
    switch (ie->type) {
    case BCV_TYPE_INTERNAL:
        map_hd_drive((void*)ie->vector);
        break;
    case BCV_TYPE_EXTERNAL:
        call_bcv(ie->vector >> 16, ie->vector & 0xffff);
        break;
    }
}

// Prepare for boot - show menu and run bcvs.
void
boot_prep(void)
{
    if (! CONFIG_BOOT) {
        wait_threads();
        return;
    }

    // XXX - show available drives?

    // Allow user to modify BCV/IPL order.
    interactive_bootmenu();
    wait_threads();

    // Setup floppy boot order
    int override = IPL.bev[0].subchoice;
    struct drive_s *tmp = Drives.idmap[EXTTYPE_FLOPPY][0];
    Drives.idmap[EXTTYPE_FLOPPY][0] = Drives.idmap[EXTTYPE_FLOPPY][override];
    Drives.idmap[EXTTYPE_FLOPPY][override] = tmp;

    // Run BCVs
    override = IPL.bev[1].subchoice;
    if (override < IPL.bcvcount)
        run_bcv(&IPL.bcv[override]);
    int i;
    for (i=0; i<IPL.bcvcount; i++)
        if (i != override)
            run_bcv(&IPL.bcv[i]);
}


/****************************************************************
 * Boot code (int 18/19)
 ****************************************************************/

// Jump to a bootup entry point.
static void
call_boot_entry(u16 bootseg, u16 bootip, u8 bootdrv)
{
    dprintf(1, "Booting from %04x:%04x\n", bootseg, bootip);
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.flags = F_IF;
    br.code = SEGOFF(bootseg, bootip);
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
    br.flags = F_IF;
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
boot_cdrom(struct ipl_entry_s *ie)
{
    if (! CONFIG_CDROM_BOOT)
        return;
    int status = cdrom_boot(ie->subchoice);
    if (status) {
        printf("Boot failed: Could not read from CDROM (code %04x)\n", status);
        return;
    }

    u16 ebda_seg = get_ebda_seg();
    u8 bootdrv = GET_EBDA2(ebda_seg, cdemu.emulated_extdrive);
    u16 bootseg = GET_EBDA2(ebda_seg, cdemu.load_segment);
    /* Canonicalize bootseg:bootip */
    u16 bootip = (bootseg & 0x0fff) << 4;
    bootseg &= 0xf000;

    call_boot_entry(bootseg, bootip, bootdrv);
}

// Boot from a CBFS payload
static void
boot_cbfs(struct ipl_entry_s *ie)
{
    if (!CONFIG_COREBOOT || !CONFIG_COREBOOT_FLASH)
        return;
    int count = ie->subchoice;
    struct cbfs_file *file = NULL;
    for (;;) {
        file = cbfs_findprefix("img/", file);
        if (!file)
            return;
        if (count--)
            continue;
        cbfs_run_payload(file);
    }
}

static void
do_boot(u16 seq_nr)
{
    if (! CONFIG_BOOT)
        panic("Boot support not compiled in.\n");

    u32 bootdev = IPL.bootorder;
    bootdev >>= 4 * seq_nr;
    bootdev &= 0xf;

    /* Translate bootdev to an IPL table offset by subtracting 1 */
    bootdev -= 1;

    if (bootdev >= IPL.bevcount) {
        printf("No bootable device.\n");
        // Loop with irqs enabled - this allows ctrl+alt+delete to work.
        for (;;)
            wait_irq();
    }

    /* Do the loading, and set up vector as a far pointer to the boot
     * address, and bootdrv as the boot drive */
    struct ipl_entry_s *ie = &IPL.bev[bootdev];
    char desc[33];
    printf("Booting from %s...\n"
           , strtcpy(desc, ie->description, ARRAY_SIZE(desc)));

    switch (ie->type) {
    case IPL_TYPE_FLOPPY:
        boot_disk(0x00, IPL.checkfloppysig);
        break;
    case IPL_TYPE_HARDDISK:
        boot_disk(0x80, 1);
        break;
    case IPL_TYPE_CDROM:
        boot_cdrom(ie);
        break;
    case IPL_TYPE_CBFS:
        boot_cbfs(ie);
        break;
    case IPL_TYPE_BEV:
        call_boot_entry(ie->vector >> 16, ie->vector & 0xffff, 0);
        break;
    }

    // Boot failed: invoke the boot recovery function
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.flags = F_IF;
    call16_int(0x18, &br);
}

// Boot Failure recovery: try the next device.
void VISIBLE32FLAT
handle_18(void)
{
    debug_serial_setup();
    debug_enter(NULL, DEBUG_HDL_18);
    u16 ebda_seg = get_ebda_seg();
    u16 seq = GET_EBDA2(ebda_seg, boot_sequence) + 1;
    SET_EBDA2(ebda_seg, boot_sequence, seq);
    do_boot(seq);
}

// INT 19h Boot Load Service Entry Point
void VISIBLE32FLAT
handle_19(void)
{
    debug_serial_setup();
    debug_enter(NULL, DEBUG_HDL_19);
    SET_EBDA(boot_sequence, 0);
    do_boot(0);
}
