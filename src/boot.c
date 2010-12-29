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
 * Boot priority ordering
 ****************************************************************/

static void
loadBootOrder(void)
{
    char *f = romfile_loadfile("bootorder", NULL);
    if (!f)
        return;

    int i;
    IPL.fw_bootorder_count = 1;
    while (f[i]) {
        if (f[i] == '\n')
            IPL.fw_bootorder_count++;
        i++;
    }
    IPL.fw_bootorder = malloc_tmphigh(IPL.fw_bootorder_count*sizeof(char*));
    if (!IPL.fw_bootorder) {
        warn_noalloc();
        free(f);
        return;
    }

    dprintf(3, "boot order:\n");
    i = 0;
    do {
        IPL.fw_bootorder[i] = f;
        f = strchr(f, '\n');
        if (f) {
            *f = '\0';
            f++;
            dprintf(3, "%d: %s\n", i, IPL.fw_bootorder[i]);
            i++;
        }
    } while(f);
}

int bootprio_find_pci_device(int bdf)
{
    return -1;
}

int bootprio_find_ata_device(int bdf, int chanid, int slave)
{
    return -1;
}

int bootprio_find_fdc_device(int bfd, int port, int fdid)
{
    return -1;
}

int bootprio_find_pci_rom(int bdf, int instance)
{
    return -1;
}

int bootprio_find_named_rom(const char *name, int instance)
{
    return -1;
}


/****************************************************************
 * Boot setup
 ****************************************************************/

static int CheckFloppySig = 1;

#define DEFAULT_PRIO           9999

static int DefaultFloppyPrio = 101;
static int DefaultCDPrio     = 102;
static int DefaultHDPrio     = 103;
static int DefaultBEVPrio    = 104;

void
boot_setup(void)
{
    if (! CONFIG_BOOT)
        return;

    SET_EBDA(boot_sequence, 0xffff);

    if (!CONFIG_COREBOOT) {
        // On emulators, get boot order from nvram.
        if (inb_cmos(CMOS_BIOS_BOOTFLAG1) & 1)
            CheckFloppySig = 0;
        u32 bootorder = (inb_cmos(CMOS_BIOS_BOOTFLAG2)
                         | ((inb_cmos(CMOS_BIOS_BOOTFLAG1) & 0xf0) << 4));
        DefaultFloppyPrio = DefaultCDPrio = DefaultHDPrio
            = DefaultBEVPrio = DEFAULT_PRIO;
        int i;
        for (i=101; i<104; i++) {
            u32 val = bootorder & 0x0f;
            bootorder >>= 4;
            switch (val) {
            case 1: DefaultFloppyPrio = i; break;
            case 2: DefaultHDPrio = i;     break;
            case 3: DefaultCDPrio = i;     break;
            case 4: DefaultBEVPrio = i;    break;
            }
        }
    }

    loadBootOrder();
}


/****************************************************************
 * BootList handling
 ****************************************************************/

struct bootentry_s {
    int type;
    union {
        u32 data;
        struct segoff_s vector;
        struct drive_s *drive;
    };
    int priority;
    const char *description;
    struct bootentry_s *next;
};

static struct bootentry_s *BootList;

static void
bootentry_add(int type, int prio, u32 data, const char *desc)
{
    if (! CONFIG_BOOT)
        return;
    struct bootentry_s *be = malloc_tmp(sizeof(*be));
    if (!be) {
        warn_noalloc();
        return;
    }
    be->type = type;
    be->priority = prio;
    be->data = data;
    be->description = desc;

    // Add entry in sorted order.
    struct bootentry_s **pprev;
    for (pprev = &BootList; *pprev; pprev = &(*pprev)->next) {
        struct bootentry_s *pos = *pprev;
        if (be->priority < pos->priority)
            break;
        if (be->priority > pos->priority)
            continue;
        if (be->type < pos->type)
            break;
        if (be->type > pos->type)
            continue;
        if (be->type <= IPL_TYPE_CDROM
            && (be->drive->type < pos->drive->type
                || (be->drive->type == pos->drive->type
                    && be->drive->cntl_id < pos->drive->cntl_id)))
            break;
    }
    be->next = *pprev;
    *pprev = be;
}

// Return the given priority if it's set - defaultprio otherwise.
static inline int defPrio(int priority, int defaultprio) {
    return (priority < 0) ? defaultprio : priority;
}

// Add a BEV vector for a given pnp compatible option rom.
void
boot_add_bev(u16 seg, u16 bev, u16 desc, int prio)
{
    bootentry_add(IPL_TYPE_BEV, defPrio(prio, DefaultBEVPrio)
                  , SEGOFF(seg, bev).segoff
                  , desc ? MAKE_FLATPTR(seg, desc) : "Unknown");
    DefaultBEVPrio = DEFAULT_PRIO;
}

// Add a bcv entry for an expansion card harddrive or legacy option rom
void
boot_add_bcv(u16 seg, u16 ip, u16 desc, int prio)
{
    bootentry_add(IPL_TYPE_BCV, defPrio(prio, DEFAULT_PRIO)
                  , SEGOFF(seg, ip).segoff
                  , desc ? MAKE_FLATPTR(seg, desc) : "Legacy option rom");
}

void
boot_add_floppy(struct drive_s *drive_g, int prio)
{
    bootentry_add(IPL_TYPE_FLOPPY, defPrio(prio, DefaultFloppyPrio)
                  , (u32)drive_g, drive_g->desc);
}

void
boot_add_hd(struct drive_s *drive_g, int prio)
{
    bootentry_add(IPL_TYPE_HARDDISK, defPrio(prio, DefaultHDPrio)
                  , (u32)drive_g, drive_g->desc);
}

void
boot_add_cd(struct drive_s *drive_g, int prio)
{
    bootentry_add(IPL_TYPE_CDROM, defPrio(prio, DefaultCDPrio)
                  , (u32)drive_g, drive_g->desc);
}

// Add a CBFS payload entry
void
boot_add_cbfs(void *data, const char *desc, int prio)
{
    bootentry_add(IPL_TYPE_CBFS, defPrio(prio, DEFAULT_PRIO), (u32)data, desc);
}


/****************************************************************
 * Boot menu and BCV execution
 ****************************************************************/

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

    // Show menu items
    struct bootentry_s *pos = BootList;
    int maxmenu = 0;
    while (pos) {
        char desc[60];
        maxmenu++;
        printf("%d. %s\n", maxmenu
               , strtcpy(desc, pos->description, ARRAY_SIZE(desc)));
        pos = pos->next;
    }

    // Get key press
    for (;;) {
        scan_code = get_keystroke(1000);
        if (scan_code >= 1 && scan_code <= maxmenu+1)
            break;
    }
    printf("\n");
    if (scan_code == 0x01)
        // ESC
        return;

    // Find entry and make top priority.
    int choice = scan_code - 1;
    struct bootentry_s **pprev = &BootList;
    while (--choice)
        pprev = &(*pprev)->next;
    pos = *pprev;
    *pprev = pos->next;
    pos->next = BootList;
    BootList = pos;
    pos->priority = 0;
}

static int HaveHDBoot, HaveFDBoot;

static void
add_bev(int type, u32 vector)
{
    if (type == IPL_TYPE_HARDDISK && HaveHDBoot++)
        return;
    if (type == IPL_TYPE_FLOPPY && HaveFDBoot++)
        return;
    if (IPL.bevcount >= ARRAY_SIZE(IPL.bev))
        return;
    struct ipl_entry_s *bev = &IPL.bev[IPL.bevcount++];
    bev->type = type;
    bev->vector = vector;
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

    // Map drives and populate BEV list
    struct bootentry_s *pos = BootList;
    while (pos) {
        switch (pos->type) {
        case IPL_TYPE_BCV:
            call_bcv(pos->vector.seg, pos->vector.offset);
            add_bev(IPL_TYPE_HARDDISK, 0);
            break;
        case IPL_TYPE_FLOPPY:
            map_floppy_drive(pos->drive);
            add_bev(IPL_TYPE_FLOPPY, 0);
            break;
        case IPL_TYPE_HARDDISK:
            map_hd_drive(pos->drive);
            add_bev(IPL_TYPE_HARDDISK, 0);
            break;
        case IPL_TYPE_CDROM:
            map_cd_drive(pos->drive);
            // NO BREAK
        default:
            add_bev(pos->type, pos->data);
            break;
        }
        pos = pos->next;
    }

    // If nothing added a floppy/hd boot - add it manually.
    add_bev(IPL_TYPE_FLOPPY, 0);
    add_bev(IPL_TYPE_HARDDISK, 0);
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

    if (!ie->vector)
        return;
    printf("Booting from DVD/CD...\n");

    struct drive_s *drive_g = (void*)ie->vector;
    int status = cdrom_boot(drive_g);
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
    printf("Booting from CBFS...\n");
    cbfs_run_payload((void*)ie->vector);
}

static void
do_boot(u16 seq_nr)
{
    if (! CONFIG_BOOT)
        panic("Boot support not compiled in.\n");

    if (seq_nr >= IPL.bevcount) {
        printf("No bootable device.\n");
        // Loop with irqs enabled - this allows ctrl+alt+delete to work.
        for (;;)
            wait_irq();
    }

    // Boot the given BEV type.
    struct ipl_entry_s *ie = &IPL.bev[seq_nr];
    switch (ie->type) {
    case IPL_TYPE_FLOPPY:
        printf("Booting from Floppy...\n");
        boot_disk(0x00, CheckFloppySig);
        break;
    case IPL_TYPE_HARDDISK:
        printf("Booting from Hard Disk...\n");
        boot_disk(0x80, 1);
        break;
    case IPL_TYPE_CDROM:
        boot_cdrom(ie);
        break;
    case IPL_TYPE_CBFS:
        boot_cbfs(ie);
        break;
    case IPL_TYPE_BEV:
        printf("Booting from ROM...\n");
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
