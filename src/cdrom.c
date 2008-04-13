// 16bit code to access cdrom drives.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "disk.h" // cdrom_13
#include "util.h" // memset
#include "ata.h" // ATA_CMD_READ_SECTORS

#define DEBUGF1(fmt, args...) bprintf(0, fmt , ##args)
#define DEBUGF(fmt, args...)


/****************************************************************
 * CDROM functions
 ****************************************************************/

// read disk drive size
static void
cdrom_1315(struct bregs *regs, u8 device)
{
    disk_ret(regs, DISK_RET_EADDRNOTFOUND);
}

// lock
static void
cdrom_134500(struct bregs *regs, u8 device)
{
    u8 locks = GET_EBDA(ata.devices[device].lock);
    if (locks == 0xff) {
        regs->al = 1;
        disk_ret(regs, DISK_RET_ETOOMANYLOCKS);
        return;
    }
    SET_EBDA(ata.devices[device].lock, locks + 1);
    regs->al = 1;
    disk_ret(regs, DISK_RET_SUCCESS);
}

// unlock
static void
cdrom_134501(struct bregs *regs, u8 device)
{
    u8 locks = GET_EBDA(ata.devices[device].lock);
    if (locks == 0x00) {
        regs->al = 0;
        disk_ret(regs, DISK_RET_ENOTLOCKED);
        return;
    }
    locks--;
    SET_EBDA(ata.devices[device].lock, locks);
    regs->al = (locks ? 1 : 0);
    disk_ret(regs, DISK_RET_SUCCESS);
}

// status
static void
cdrom_134502(struct bregs *regs, u8 device)
{
    u8 locks = GET_EBDA(ata.devices[device].lock);
    regs->al = (locks ? 1 : 0);
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
cdrom_1345XX(struct bregs *regs, u8 device)
{
    disk_ret(regs, DISK_RET_EPARAM);
}

// IBM/MS lock/unlock drive
static void
cdrom_1345(struct bregs *regs, u8 device)
{
    switch (regs->al) {
    case 0x00: cdrom_134500(regs, device); break;
    case 0x01: cdrom_134501(regs, device); break;
    case 0x02: cdrom_134502(regs, device); break;
    default:   cdrom_1345XX(regs, device); break;
    }
}

// IBM/MS eject media
static void
cdrom_1346(struct bregs *regs, u8 device)
{
    u8 locks = GET_EBDA(ata.devices[device].lock);
    if (locks != 0) {
        disk_ret(regs, DISK_RET_ELOCKED);
        return;
    }

    // FIXME should handle 0x31 no media in device
    // FIXME should handle 0xb5 valid request failed

    // Call removable media eject
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ah = 0x52;
    call16_int(0x15, &br);

    if (br.ah || br.flags & F_CF) {
        disk_ret(regs, DISK_RET_ELOCKED);
        return;
    }
    disk_ret(regs, DISK_RET_SUCCESS);
}

// IBM/MS extended media change
static void
cdrom_1349(struct bregs *regs, u8 device)
{
    set_fail(regs);
    // always send changed ??
    regs->ah = DISK_RET_ECHANGED;
}

static void
cdrom_ok(struct bregs *regs, u8 device)
{
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
cdrom_wp(struct bregs *regs, u8 device)
{
    disk_ret(regs, DISK_RET_EWRITEPROTECT);
}

void
cdrom_13(struct bregs *regs, u8 device)
{
    //debug_stub(regs);

    switch (regs->ah) {
    case 0x15: cdrom_1315(regs, device); break;
    case 0x45: cdrom_1345(regs, device); break;
    case 0x46: cdrom_1346(regs, device); break;
    case 0x49: cdrom_1349(regs, device); break;

    // These functions are the same as for hard disks
    case 0x01:
    case 0x41:
    case 0x42:
    case 0x44:
    case 0x47:
    case 0x48:
    case 0x4e:
        disk_13(regs, device);
        break;

    // all these functions return SUCCESS
    case 0x00: // disk controller reset
    case 0x09: // initialize drive parameters
    case 0x0c: // seek to specified cylinder
    case 0x0d: // alternate disk reset
    case 0x10: // check drive ready
    case 0x11: // recalibrate
    case 0x14: // controller internal diagnostic
    case 0x16: // detect disk change
        cdrom_ok(regs, device);
        break;

    // all these functions return disk write-protected
    case 0x03: // write disk sectors
    case 0x05: // format disk track
    case 0x43: // IBM/MS extended write
        cdrom_wp(regs, device);
        break;

    default:   disk_13XX(regs, device); break;
    }
}


/****************************************************************
 * CD emulation
 ****************************************************************/

// Read a series of 512 byte sectors from the cdrom starting at the
// image offset.
inline int
cdrom_read_emu(u16 biosid, u32 vlba, u32 count, void *far_buffer)
{
    u32 ilba = GET_EBDA(cdemu.ilba);
    return cdrom_read_512(biosid, ilba * 4 + vlba, count, far_buffer);
}

// read disk drive parameters
static void
cdemu_1308(struct bregs *regs, u8 device)
{
    u16 nlc   = GET_EBDA(cdemu.vdevice.cylinders) - 1;
    u16 nlh   = GET_EBDA(cdemu.vdevice.heads) - 1;
    u16 nlspt = GET_EBDA(cdemu.vdevice.spt);

    regs->al = 0x00;
    regs->bl = 0x00;
    regs->ch = nlc & 0xff;
    regs->cl = ((nlc >> 2) & 0xc0) | (nlspt  & 0x3f);
    regs->dh = nlh;
    // FIXME ElTorito Various. should send the real count of drives 1 or 2
    // FIXME ElTorito Harddisk. should send the HD count
    regs->dl = 0x02;
    u8 media = GET_EBDA(cdemu.media);
    if (media <= 3)
        regs->bl = media * 2;

    regs->es = SEG_BIOS;
    regs->di = (u32)&diskette_param_table2;

    disk_ret(regs, DISK_RET_SUCCESS);
}

void
cdemu_13(struct bregs *regs)
{
    //debug_stub(regs);

    u8 device  = GET_EBDA(cdemu.controller_index) * 2;
    device += GET_EBDA(cdemu.device_spec);

    switch (regs->ah) {
    // These functions are the same as for hard disks
    case 0x02:
    case 0x04:
        disk_13(regs, device);
        break;

    // These functions are the same as standard CDROM.
    case 0x00:
    case 0x01:
    case 0x03:
    case 0x05:
    case 0x09:
    case 0x0c:
    case 0x0d:
    case 0x10:
    case 0x11:
    case 0x14:
    case 0x15:
    case 0x16:
        cdrom_13(regs, device);
        break;

    case 0x08: cdemu_1308(regs, device); break;

    default:   disk_13XX(regs, device); break;
    }
}

struct eltorito_s {
    u8 size;
    u8 media;
    u8 emulated_drive;
    u8 controller_index;
    u32 ilba;
    u16 device_spec;
    u16 buffer_segment;
    u16 load_segment;
    u16 sector_count;
    u8 cylinders;
    u8 sectors;
    u8 heads;
};

#define SET_INT13ET(regs,var,val)                                      \
    SET_FARVAR((regs)->ds, ((struct eltorito_s*)((regs)->si+0))->var, (val))

// ElTorito - Terminate disk emu
void
cdemu_134b(struct bregs *regs)
{
    // FIXME ElTorito Hardcoded
    SET_INT13ET(regs, size, 0x13);
    SET_INT13ET(regs, media, GET_EBDA(cdemu.media));
    SET_INT13ET(regs, emulated_drive, GET_EBDA(cdemu.emulated_drive));
    SET_INT13ET(regs, controller_index, GET_EBDA(cdemu.controller_index));
    SET_INT13ET(regs, ilba, GET_EBDA(cdemu.ilba));
    SET_INT13ET(regs, device_spec, GET_EBDA(cdemu.device_spec));
    SET_INT13ET(regs, buffer_segment, GET_EBDA(cdemu.buffer_segment));
    SET_INT13ET(regs, load_segment, GET_EBDA(cdemu.load_segment));
    SET_INT13ET(regs, sector_count, GET_EBDA(cdemu.sector_count));
    SET_INT13ET(regs, cylinders, GET_EBDA(cdemu.vdevice.cylinders));
    SET_INT13ET(regs, sectors, GET_EBDA(cdemu.vdevice.spt));
    SET_INT13ET(regs, heads, GET_EBDA(cdemu.vdevice.heads));

    // If we have to terminate emulation
    if (regs->al == 0x00) {
        // FIXME ElTorito Various. Should be handled accordingly to spec
        SET_EBDA(cdemu.active, 0x00); // bye bye
    }

    disk_ret(regs, DISK_RET_SUCCESS);
}


/****************************************************************
 * CD booting
 ****************************************************************/

// Request SENSE
static u16
atapi_get_sense(u16 device, u8 *asc, u8 *ascq)
{
    u8 buffer[18];
    u8 atacmd[12];
    memset(atacmd, 0, sizeof(atacmd));
    atacmd[0] = ATA_CMD_REQUEST_SENSE;
    atacmd[4] = sizeof(buffer);
    u16 ret = ata_cmd_packet(device, atacmd, sizeof(atacmd), sizeof(buffer)
                             , MAKE_FARPTR(GET_SEG(SS), (u32)buffer));
    if (ret != 0)
        return 0x0002;

    *asc = buffer[12];
    *ascq = buffer[13];

    return 0;
}

static u16
atapi_is_ready(u16 device)
{
    if (GET_EBDA(ata.devices[device].type) != ATA_TYPE_ATAPI) {
        printf("not implemented for non-ATAPI device\n");
        return -1;
    }

    DEBUGF("ata_detect_medium: begin\n");
    u8 packet[12];
    memset(packet, 0, sizeof(packet));
    packet[0] = 0x25; /* READ CAPACITY */

    /* Retry READ CAPACITY 50 times unless MEDIUM NOT PRESENT
     * is reported by the device. If the device reports "IN PROGRESS",
     * 30 seconds is added. */
    u8 buf[8];
    u32 timeout = 5000;
    u32 time = 0;
    u8 in_progress = 0;
    for (;; time+=100) {
        if (time >= timeout) {
            DEBUGF("read capacity failed\n");
            return -1;
        }
        u16 ret = ata_cmd_packet(device, packet, sizeof(packet), sizeof(buf)
                                 , MAKE_FARPTR(GET_SEG(SS), (u32)buf));
        if (ret == 0)
            break;

        u8 asc=0, ascq=0;
        ret = atapi_get_sense(device, &asc, &ascq);
        if (!ret)
            continue;

        if (asc == 0x3a) { /* MEDIUM NOT PRESENT */
            DEBUGF("Device reports MEDIUM NOT PRESENT\n");
            return -1;
        }

        if (asc == 0x04 && ascq == 0x01 && !in_progress) {
            /* IN PROGRESS OF BECOMING READY */
            printf("Waiting for device to detect medium... ");
            /* Allow 30 seconds more */
            timeout = 30000;
            in_progress = 1;
        }
    }

    u32 block_len = (u32) buf[4] << 24
        | (u32) buf[5] << 16
        | (u32) buf[6] << 8
        | (u32) buf[7] << 0;

    if (block_len != 2048 && block_len != 512) {
        printf("Unsupported sector size %u\n", block_len);
        return -1;
    }
    SET_EBDA(ata.devices[device].blksize, block_len);

    u32 sectors = (u32) buf[0] << 24
        | (u32) buf[1] << 16
        | (u32) buf[2] << 8
        | (u32) buf[3] << 0;

    DEBUGF("sectors=%u\n", sectors);
    if (block_len == 2048)
        sectors <<= 2; /* # of sectors in 512-byte "soft" sector */
    if (sectors != GET_EBDA(ata.devices[device].sectors))
        printf("%dMB medium detected\n", sectors>>(20-9));
    SET_EBDA(ata.devices[device].sectors, sectors);
    return 0;
}

static u16
atapi_is_cdrom(u8 device)
{
    if (device >= CONFIG_MAX_ATA_DEVICES)
        return 0;

    if (GET_EBDA(ata.devices[device].type) != ATA_TYPE_ATAPI)
        return 0;

    if (GET_EBDA(ata.devices[device].device) != ATA_DEVICE_CDROM)
        return 0;

    return 1;
}

// Compare a string on the stack to one in the code segment.
static int
streq_cs(u8 *s1, char *cs_s2)
{
    u8 *s2 = (u8*)cs_s2;
    for (;;) {
        if (*s1 != GET_VAR(CS, *s2))
            return 0;
        if (! *s1)
            return 1;
        s1++;
        s2++;
    }
}

u16
cdrom_boot()
{
    // Find out the first cdrom
    u8 device;
    for (device=0; device<CONFIG_MAX_ATA_DEVICES; device++)
        if (atapi_is_cdrom(device))
            break;

    u16 ret = atapi_is_ready(device);
    if (ret)
        BX_INFO("ata_is_ready returned %d\n", ret);

    // if not found
    if (device >= CONFIG_MAX_ATA_DEVICES)
        return 2;

    // Read the Boot Record Volume Descriptor
    u8 buffer[2048];
    ret = cdrom_read(device, 0x11, 1
                     , MAKE_FARPTR(GET_SEG(SS), (u32)buffer));
    if (ret)
        return 3;

    // Validity checks
    if (buffer[0])
        return 4;
    if (!streq_cs(&buffer[1], "CD001\001EL TORITO SPECIFICATION"))
        return 5;

    // ok, now we calculate the Boot catalog address
    u32 lba = *(u32*)&buffer[0x47];

    // And we read the Boot Catalog
    ret = cdrom_read(device, lba, 1
                     , MAKE_FARPTR(GET_SEG(SS), (u32)buffer));
    if (ret)
        return 7;

    // Validation entry
    if (buffer[0x00] != 0x01)
        return 8;   // Header
    if (buffer[0x01] != 0x00)
        return 9;   // Platform
    if (buffer[0x1E] != 0x55)
        return 10;  // key 1
    if (buffer[0x1F] != 0xAA)
        return 10;  // key 2

    // Initial/Default Entry
    if (buffer[0x20] != 0x88)
        return 11; // Bootable

    u8 media = buffer[0x21];
    SET_EBDA(cdemu.media, media);

    SET_EBDA(cdemu.controller_index, device/2);
    SET_EBDA(cdemu.device_spec, device%2);

    u16 boot_segment = *(u16*)&buffer[0x22];
    if (!boot_segment)
        boot_segment = 0x07C0;
    SET_EBDA(cdemu.load_segment, boot_segment);
    SET_EBDA(cdemu.buffer_segment, 0x0000);

    u16 nbsectors = *(u16*)&buffer[0x26];
    SET_EBDA(cdemu.sector_count, nbsectors);

    lba = *(u32*)&buffer[0x28];
    SET_EBDA(cdemu.ilba, lba);

    // And we read the image in memory
    ret = cdrom_read_emu(device, 0, nbsectors, MAKE_FARPTR(boot_segment, 0));
    if (ret)
        return 12;

    if (media == 0) {
        // No emulation requested - return success.

        // FIXME ElTorito Hardcoded. cdrom is hardcoded as device 0xE0.
        // Win2000 cd boot needs to know it booted from cd
        SET_EBDA(cdemu.emulated_drive, 0xE0);

        return 0;
    }

    // Emulation of a floppy/harddisk requested
    if (! CONFIG_CDROM_EMU)
        return 13;

    // Set emulated drive id and increase bios installed hardware
    // number of devices
    if (media < 4) {
        // Floppy emulation
        SET_EBDA(cdemu.emulated_drive, 0x00);
        SETBITS_BDA(equipment_list_flags, 0x41);
    } else {
        // Harddrive emulation
        SET_EBDA(cdemu.emulated_drive, 0x80);
        SET_EBDA(ata.hdcount, GET_EBDA(ata.hdcount) + 1);
    }

    // Remember the media type
    switch (media) {
    case 0x01:  // 1.2M floppy
        SET_EBDA(cdemu.vdevice.spt, 15);
        SET_EBDA(cdemu.vdevice.cylinders, 80);
        SET_EBDA(cdemu.vdevice.heads, 2);
        break;
    case 0x02:  // 1.44M floppy
        SET_EBDA(cdemu.vdevice.spt, 18);
        SET_EBDA(cdemu.vdevice.cylinders, 80);
        SET_EBDA(cdemu.vdevice.heads, 2);
        break;
    case 0x03:  // 2.88M floppy
        SET_EBDA(cdemu.vdevice.spt, 36);
        SET_EBDA(cdemu.vdevice.cylinders, 80);
        SET_EBDA(cdemu.vdevice.heads, 2);
        break;
    case 0x04: { // Harddrive
        u16 spt = GET_FARVAR(boot_segment,*(u8*)(446+6));
        u16 cyl = (spt << 2) + GET_FARVAR(boot_segment,*(u8*)(446+7)) + 1;
        u16 heads = GET_FARVAR(boot_segment,*(u8*)(446+5)) + 1;
        SET_EBDA(cdemu.vdevice.spt, spt & 0x3f);
        SET_EBDA(cdemu.vdevice.cylinders, cyl);
        SET_EBDA(cdemu.vdevice.heads, heads);
        break;
    }
    }

    // everything is ok, so from now on, the emulation is active
    SET_EBDA(cdemu.active, 0x01);
    DEBUGF("cdemu media=%d\n", media);

    return 0;
}
