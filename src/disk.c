// 16bit code to access hard drives.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "disk.h" // floppy_13
#include "biosvar.h" // struct bregs
#include "config.h" // CONFIG_*
#include "cmos.h" // inb_cmos
#include "util.h" // debug_enter
#include "ata.h" // ATA_*


// ---------------------------------------------------------------------------
// ATA/ATAPI driver : device detection
// ---------------------------------------------------------------------------

void
ata_detect()
{
    u8  hdcount, cdcount, device, type;
    u8  buffer[0x0200];

#if CONFIG_MAX_ATA_INTERFACES > 0
    SET_EBDA(ata.channels[0].iface,ATA_IFACE_ISA);
    SET_EBDA(ata.channels[0].iobase1,0x1f0);
    SET_EBDA(ata.channels[0].iobase2,0x3f0);
    SET_EBDA(ata.channels[0].irq,14);
#endif
#if CONFIG_MAX_ATA_INTERFACES > 1
    SET_EBDA(ata.channels[1].iface,ATA_IFACE_ISA);
    SET_EBDA(ata.channels[1].iobase1,0x170);
    SET_EBDA(ata.channels[1].iobase2,0x370);
    SET_EBDA(ata.channels[1].irq,15);
#endif
#if CONFIG_MAX_ATA_INTERFACES > 2
    SET_EBDA(ata.channels[2].iface,ATA_IFACE_ISA);
    SET_EBDA(ata.channels[2].iobase1,0x1e8);
    SET_EBDA(ata.channels[2].iobase2,0x3e0);
    SET_EBDA(ata.channels[2].irq,12);
#endif
#if CONFIG_MAX_ATA_INTERFACES > 3
    SET_EBDA(ata.channels[3].iface,ATA_IFACE_ISA);
    SET_EBDA(ata.channels[3].iobase1,0x168);
    SET_EBDA(ata.channels[3].iobase2,0x360);
    SET_EBDA(ata.channels[3].irq,11);
#endif
#if CONFIG_MAX_ATA_INTERFACES > 4
#error Please fill the ATA interface informations
#endif

    // Device detection
    hdcount=cdcount=0;

    for(device=0; device<CONFIG_MAX_ATA_DEVICES; device++) {
        u16 iobase1, iobase2;
        u8  channel, slave, shift;
        u8  sc, sn, cl, ch, st;

        channel = device / 2;
        slave = device % 2;

        iobase1 =GET_EBDA(ata.channels[channel].iobase1);
        iobase2 =GET_EBDA(ata.channels[channel].iobase2);

        // Disable interrupts
        outb(ATA_CB_DC_HD15 | ATA_CB_DC_NIEN, iobase2+ATA_CB_DC);

        // Look for device
        outb(slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0, iobase1+ATA_CB_DH);
        outb(0x55, iobase1+ATA_CB_SC);
        outb(0xaa, iobase1+ATA_CB_SN);
        outb(0xaa, iobase1+ATA_CB_SC);
        outb(0x55, iobase1+ATA_CB_SN);
        outb(0x55, iobase1+ATA_CB_SC);
        outb(0xaa, iobase1+ATA_CB_SN);

        // If we found something
        sc = inb(iobase1+ATA_CB_SC);
        sn = inb(iobase1+ATA_CB_SN);

        if ( (sc == 0x55) && (sn == 0xaa) ) {
            SET_EBDA(ata.devices[device].type,ATA_TYPE_UNKNOWN);

            // reset the channel
            ata_reset(device);

            // check for ATA or ATAPI
            outb(slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0, iobase1+ATA_CB_DH);
            sc = inb(iobase1+ATA_CB_SC);
            sn = inb(iobase1+ATA_CB_SN);
            if ((sc==0x01) && (sn==0x01)) {
                cl = inb(iobase1+ATA_CB_CL);
                ch = inb(iobase1+ATA_CB_CH);
                st = inb(iobase1+ATA_CB_STAT);

                if ((cl==0x14) && (ch==0xeb)) {
                    SET_EBDA(ata.devices[device].type,ATA_TYPE_ATAPI);
                } else if ((cl==0x00) && (ch==0x00) && (st!=0x00)) {
                    SET_EBDA(ata.devices[device].type,ATA_TYPE_ATA);
                } else if ((cl==0xff) && (ch==0xff)) {
                    SET_EBDA(ata.devices[device].type,ATA_TYPE_NONE);
                }
            }
        }

        type=GET_EBDA(ata.devices[device].type);

        // Now we send a IDENTIFY command to ATA device
        if(type == ATA_TYPE_ATA) {
            u32 sectors;
            u16 cylinders, heads, spt, blksize;
            u8  translation, removable, mode;

            //Temporary values to do the transfer
            SET_EBDA(ata.devices[device].device,ATA_DEVICE_HD);
            SET_EBDA(ata.devices[device].mode, ATA_MODE_PIO16);

            u16 ret = ata_cmd_data_in(device,ATA_CMD_IDENTIFY_DEVICE
                                      , 1, 0, 0, 0, 0L
                                      , GET_SEG(SS), (u32)buffer);
            if (ret !=0)
                BX_PANIC("ata-detect: Failed to detect ATA device\n");

            removable = (buffer[0] & 0x80) ? 1 : 0;
            mode      = buffer[96] ? ATA_MODE_PIO32 : ATA_MODE_PIO16;
            blksize   = *(u16*)&buffer[10];

            cylinders = *(u16*)&buffer[1*2]; // word 1
            heads     = *(u16*)&buffer[3*2]; // word 3
            spt       = *(u16*)&buffer[6*2]; // word 6

            sectors   = *(u32*)&buffer[60*2]; // word 60 and word 61

            SET_EBDA(ata.devices[device].device,ATA_DEVICE_HD);
            SET_EBDA(ata.devices[device].removable, removable);
            SET_EBDA(ata.devices[device].mode, mode);
            SET_EBDA(ata.devices[device].blksize, blksize);
            SET_EBDA(ata.devices[device].pchs.heads, heads);
            SET_EBDA(ata.devices[device].pchs.cylinders, cylinders);
            SET_EBDA(ata.devices[device].pchs.spt, spt);
            SET_EBDA(ata.devices[device].sectors, sectors);
            BX_INFO("ata%d-%d: PCHS=%u/%d/%d translation=", channel, slave,cylinders, heads, spt);

            translation = inb_cmos(0x39 + channel/2);
            for (shift=device%4; shift>0; shift--)
                translation >>= 2;
            translation &= 0x03;

            SET_EBDA(ata.devices[device].translation, translation);

            switch (translation) {
            case ATA_TRANSLATION_NONE:
                BX_INFO("none");
                break;
            case ATA_TRANSLATION_LBA:
                BX_INFO("lba");
                break;
            case ATA_TRANSLATION_LARGE:
                BX_INFO("large");
                break;
            case ATA_TRANSLATION_RECHS:
                BX_INFO("r-echs");
                break;
            }
            switch (translation) {
            case ATA_TRANSLATION_NONE:
                break;
            case ATA_TRANSLATION_LBA:
                spt = 63;
                sectors /= 63;
                heads = sectors / 1024;
                if (heads>128) heads = 255;
                else if (heads>64) heads = 128;
                else if (heads>32) heads = 64;
                else if (heads>16) heads = 32;
                else heads=16;
                cylinders = sectors / heads;
                break;
            case ATA_TRANSLATION_RECHS:
                // Take care not to overflow
                if (heads==16) {
                    if(cylinders>61439) cylinders=61439;
                    heads=15;
                    cylinders = (u16)((u32)(cylinders)*16/15);
                }
                // then go through the large bitshift process
            case ATA_TRANSLATION_LARGE:
                while(cylinders > 1024) {
                    cylinders >>= 1;
                    heads <<= 1;

                    // If we max out the head count
                    if (heads > 127) break;
                }
                break;
            }
            // clip to 1024 cylinders in lchs
            if (cylinders > 1024) cylinders=1024;
            BX_INFO(" LCHS=%d/%d/%d\n", cylinders, heads, spt);

            SET_EBDA(ata.devices[device].lchs.heads, heads);
            SET_EBDA(ata.devices[device].lchs.cylinders, cylinders);
            SET_EBDA(ata.devices[device].lchs.spt, spt);

            // fill hdidmap
            SET_EBDA(ata.hdidmap[hdcount], device);
            hdcount++;
        }

        // Now we send a IDENTIFY command to ATAPI device
        if(type == ATA_TYPE_ATAPI) {

            u8  type, removable, mode;
            u16 blksize;

            //Temporary values to do the transfer
            SET_EBDA(ata.devices[device].device,ATA_DEVICE_CDROM);
            SET_EBDA(ata.devices[device].mode, ATA_MODE_PIO16);

            u16 ret = ata_cmd_data_in(device,ATA_CMD_IDENTIFY_DEVICE_PACKET
                                      , 1, 0, 0, 0, 0L
                                      , GET_SEG(SS), (u32)buffer);
            if (ret != 0)
                BX_PANIC("ata-detect: Failed to detect ATAPI device\n");

            type      = buffer[1] & 0x1f;
            removable = (buffer[0] & 0x80) ? 1 : 0;
            mode      = buffer[96] ? ATA_MODE_PIO32 : ATA_MODE_PIO16;
            blksize   = 2048;

            SET_EBDA(ata.devices[device].device, type);
            SET_EBDA(ata.devices[device].removable, removable);
            SET_EBDA(ata.devices[device].mode, mode);
            SET_EBDA(ata.devices[device].blksize, blksize);

            // fill cdidmap
            SET_EBDA(ata.cdidmap[cdcount], device);
            cdcount++;
        }

        u32 sizeinmb = 0;
        u16 ataversion;
        u8  c, i, version=0, model[41];

        switch (type) {
        case ATA_TYPE_ATA:
            sizeinmb = GET_EBDA(ata.devices[device].sectors);
            sizeinmb >>= 11;
        case ATA_TYPE_ATAPI:
            // Read ATA/ATAPI version
            ataversion=((u16)(buffer[161])<<8) | buffer[160];
            for(version=15;version>0;version--) {
                if ((ataversion&(1<<version))!=0)
                    break;
            }

            // Read model name
            for (i=0;i<20;i++) {
                model[i*2] = buffer[(i*2)+54+1];
                model[(i*2)+1] = buffer[(i*2)+54];
            }

            // Reformat
            model[40] = 0x00;
            for (i=39;i>0;i--) {
                if (model[i]==0x20)
                    model[i] = 0x00;
                else
                    break;
            }
            break;
        }

        switch (type) {
        case ATA_TYPE_ATA:
            printf("ata%d %s: ",channel,slave?" slave":"master");
            i=0;
            while ((c=model[i++]))
                printf("%c",c);
            if (sizeinmb < (1UL<<16))
                printf(" ATA-%d Hard-Disk (%4u MBytes)\n", version, (u16)sizeinmb);
            else
                printf(" ATA-%d Hard-Disk (%4u GBytes)\n", version, (u16)(sizeinmb>>10));
            break;
        case ATA_TYPE_ATAPI:
            printf("ata%d %s: ",channel,slave?" slave":"master");
            i=0;
            while ((c=model[i++]))
                printf("%c",c);
            if (GET_EBDA(ata.devices[device].device)==ATA_DEVICE_CDROM)
                printf(" ATAPI-%d CD-Rom/DVD-Rom\n",version);
            else
                printf(" ATAPI-%d Device\n",version);
            break;
        case ATA_TYPE_UNKNOWN:
            printf("ata%d %s: Unknown device\n",channel,slave?" slave":"master");
            break;
        }
    }

    // Store the devices counts
    SET_EBDA(ata.hdcount, hdcount);
    SET_EBDA(ata.cdcount, cdcount);
    SET_BDA(disk_count, hdcount);

    printf("\n");

    // FIXME : should use bios=cmos|auto|disable bits
    // FIXME : should know about translation bits
    // FIXME : move hard_drive_post here

}


static inline void
disk_ret(struct bregs *regs, u8 code)
{
    regs->ah = code;
    SET_BDA(disk_last_status, code);
    set_cf(regs, code);
}

// disk controller reset
static void
disk_1300(struct bregs *regs, u8 device)
{
    ata_reset(device);
}

// read disk status
static void
disk_1301(struct bregs *regs, u8 device)
{
    regs->ah = GET_BDA(disk_last_status);
    disk_ret(regs, DISK_RET_SUCCESS);
}

static int
check_params(struct bregs *regs, u8 device)
{
    debug_stub(regs);

    u16 count       = regs->al;
    u16 cylinder    = regs->ch | ((((u16) regs->cl) << 2) & 0x300);
    u16 sector      = regs->cl & 0x3f;
    u16 head        = regs->dh;

    if ((count > 128) || (count == 0) || (sector == 0)) {
        BX_INFO("int13_harddisk: function %02x, parameter out of range!\n"
                , regs->ah);
        disk_ret(regs, DISK_RET_EPARAM);
        return -1;
    }

    u16 nlc   = GET_EBDA(ata.devices[device].lchs.cylinders);
    u16 nlh   = GET_EBDA(ata.devices[device].lchs.heads);
    u16 nlspt = GET_EBDA(ata.devices[device].lchs.spt);

    bprintf(0, "dev=%d c=%d h=%d s=%d nc=%d nh=%d ns=%d\n"
            , device, cylinder, head, sector
            , nlc, nlh, nlspt);

    // sanity check on cyl heads, sec
    if ( (cylinder >= nlc) || (head >= nlh) || (sector > nlspt )) {
        BX_INFO("int13_harddisk: function %02x, parameters out of"
                " range %04x/%04x/%04x!\n"
                , regs->ah, cylinder, head, sector);
        disk_ret(regs, DISK_RET_EPARAM);
        return -1;
    }
    return 0;
}

static void
disk_1302(struct bregs *regs, u8 device)
{
    debug_stub(regs);
    int ret = check_params(regs, device);
    if (ret)
        return;
    u16 count       = regs->al;
    u16 cylinder    = regs->ch | ((((u16) regs->cl) << 2) & 0x300);
    u16 sector      = regs->cl & 0x3f;
    u16 head        = regs->dh;
    u16 nph   = GET_EBDA(ata.devices[device].pchs.heads);
    u16 npspt = GET_EBDA(ata.devices[device].pchs.spt);

    u16 nlh   = GET_EBDA(ata.devices[device].lchs.heads);
    u16 nlspt = GET_EBDA(ata.devices[device].lchs.spt);

    u32 lba = 0;
    // if needed, translate lchs to lba, and execute command
    if ( (nph != nlh) || (npspt != nlspt)) {
        lba = (((((u32)cylinder * (u32)nlh) + (u32)head) * (u32)nlspt)
               + (u32)sector - 1);
        sector = 0; // this forces the command to be lba
    }

    u16 segment = regs->es;
    u16 offset  = regs->bx;

    u8 status = ata_cmd_data_in(device, ATA_CMD_READ_SECTORS
                                , count, cylinder, head, sector
                                , lba, segment, offset);

    // Set nb of sector transferred
    regs->al = GET_EBDA(ata.trsfsectors);

    if (status != 0) {
        BX_INFO("int13_harddisk: function %02x, error %02x !\n",regs->ah,status);
        disk_ret(regs, DISK_RET_EBADTRACK);
    }
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
disk_1303(struct bregs *regs, u8 device)
{
    debug_stub(regs);
    int ret = check_params(regs, device);
    if (ret)
        return;
    u16 count       = regs->al;
    u16 cylinder    = regs->ch | ((((u16) regs->cl) << 2) & 0x300);
    u16 sector      = regs->cl & 0x3f;
    u16 head        = regs->dh;
    u16 nph   = GET_EBDA(ata.devices[device].pchs.heads);
    u16 npspt = GET_EBDA(ata.devices[device].pchs.spt);

    u16 nlh   = GET_EBDA(ata.devices[device].lchs.heads);
    u16 nlspt = GET_EBDA(ata.devices[device].lchs.spt);

    u32 lba = 0;
    // if needed, translate lchs to lba, and execute command
    if ( (nph != nlh) || (npspt != nlspt)) {
        lba = (((((u32)cylinder * (u32)nlh) + (u32)head) * (u32)nlspt)
               + (u32)sector - 1);
        sector = 0; // this forces the command to be lba
    }

    u16 segment = regs->es;
    u16 offset  = regs->bx;

    u8 status = ata_cmd_data_out(device, ATA_CMD_READ_SECTORS
                                 , count, cylinder, head, sector
                                 , lba, segment, offset);

    // Set nb of sector transferred
    regs->al = GET_EBDA(ata.trsfsectors);

    if (status != 0) {
        BX_INFO("int13_harddisk: function %02x, error %02x !\n",regs->ah,status);
        disk_ret(regs, DISK_RET_EBADTRACK);
    }
    disk_ret(regs, DISK_RET_SUCCESS);
}

static void
disk_1304(struct bregs *regs, u8 device)
{
    int ret = check_params(regs, device);
    if (ret)
        return;
    // FIXME verify
    disk_ret(regs, DISK_RET_SUCCESS);
}

#define DISK_STUB(regs) do {                    \
        struct bregs *__regs = (regs);          \
        debug_stub(__regs);                     \
        disk_ret(__regs, DISK_RET_SUCCESS);     \
    } while (0)

// format disk track
static void
disk_1305(struct bregs *regs, u8 device)
{
    DISK_STUB(regs);
}

// read disk drive parameters
static void
disk_1308(struct bregs *regs, u8 device)
{
    // Get logical geometry from table
    u16 nlc = GET_EBDA(ata.devices[device].lchs.cylinders);
    u16 nlh = GET_EBDA(ata.devices[device].lchs.heads);
    u16 nlspt = GET_EBDA(ata.devices[device].lchs.spt);
    u16 count = GET_EBDA(ata.hdcount);

    nlc = nlc - 2; /* 0 based , last sector not used */
    regs->al = 0;
    regs->ch = nlc & 0xff;
    regs->cl = ((nlc >> 2) & 0xc0) | (nlspt & 0x3f);
    regs->dh = nlh - 1;
    regs->dl = count; /* FIXME returns 0, 1, or n hard drives */

    // FIXME should set ES & DI
    disk_ret(regs, DISK_RET_SUCCESS);

    debug_stub(regs);
}

// check drive ready
static void
disk_1310(struct bregs *regs, u8 device)
{
    // should look at 40:8E also???

    // Read the status from controller
    u8 status = inb(GET_EBDA(ata.channels[device/2].iobase1) + ATA_CB_STAT);
    if ( (status & ( ATA_CB_STAT_BSY | ATA_CB_STAT_RDY )) == ATA_CB_STAT_RDY )
        disk_ret(regs, DISK_RET_SUCCESS);
    else
        disk_ret(regs, DISK_RET_ENOTREADY);
}

// read disk drive size
static void
disk_1315(struct bregs *regs, u8 device)
{
    // Get logical geometry from table
    u16 nlc   = GET_EBDA(ata.devices[device].lchs.cylinders);
    u16 nlh   = GET_EBDA(ata.devices[device].lchs.heads);
    u16 nlspt = GET_EBDA(ata.devices[device].lchs.spt);

    // Compute sector count seen by int13
    u32 lba = (u32)(nlc - 1) * (u32)nlh * (u32)nlspt;
    regs->cx = lba >> 16;
    regs->dx = lba & 0xffff;

    disk_ret(regs, 0);
    regs->ah = 3; // hard disk accessible
}

static void
disk_13XX(struct bregs *regs, u8 device)
{
    BX_INFO("int13_harddisk: function %02xh unsupported, returns fail\n", regs->ah);
    disk_ret(regs, DISK_RET_EPARAM);
}

static void
disk_13(struct bregs *regs, u8 drive)
{
    if (! CONFIG_ATA) {
        disk_13XX(regs, drive);
        return;
    }

    debug_stub(regs);

    // clear completion flag
    SET_BDA(disk_interrupt_flag, 0);

    // basic check : device has to be defined
    if (drive < 0x80 || drive >= 0x80 + CONFIG_MAX_ATA_DEVICES) {
        disk_13XX(regs, drive);
        return;
    }

    // Get the ata channel
    u8 device = GET_EBDA(ata.hdidmap[drive-0x80]);

    // basic check : device has to be valid
    if (device >= CONFIG_MAX_ATA_DEVICES) {
        disk_13XX(regs, drive);
        return;
    }

    switch (regs->ah) {
    case 0x00: disk_1300(regs, device); break;
    case 0x01: disk_1301(regs, device); break;
    case 0x02: disk_1302(regs, device); break;
    case 0x03: disk_1303(regs, device); break;
    case 0x04: disk_1304(regs, device); break;
    case 0x05: disk_1305(regs, device); break;
    case 0x08: disk_1308(regs, device); break;
    case 0x10: disk_1310(regs, device); break;
    case 0x15: disk_1315(regs, device); break;
    // XXX - several others defined
    default:   disk_13XX(regs, device); break;
    }
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
    // XXX
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
