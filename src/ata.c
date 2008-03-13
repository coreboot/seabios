// Low level ATA disk access
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "ata.h" // ATA_*
#include "types.h" // u8
#include "ioport.h" // inb
#include "util.h" // BX_INFO
#include "cmos.h" // inb_cmos

#define TIMEOUT 0
#define BSY 1
#define NOT_BSY 2
#define NOT_BSY_DRQ 3
#define NOT_BSY_NOT_DRQ 4
#define NOT_BSY_RDY 5

#define IDE_TIMEOUT 32000u //32 seconds max for IDE ops

#define DEBUGF1(fmt, args...) bprintf(0, fmt , ##args)
#define DEBUGF(fmt, args...)

// XXX - lots of redundancy in this file.

static int
await_ide(u8 when_done, u16 base, u16 timeout)
{
    u32 time=0, last=0;
    // for the times you're supposed to throw one away
    u16 status = inb(base + ATA_CB_STAT);
    for (;;) {
        status = inb(base+ATA_CB_STAT);
        time++;
        u8 result;
        if (when_done == BSY)
            result = status & ATA_CB_STAT_BSY;
        else if (when_done == NOT_BSY)
            result = !(status & ATA_CB_STAT_BSY);
        else if (when_done == NOT_BSY_DRQ)
            result = !(status & ATA_CB_STAT_BSY) && (status & ATA_CB_STAT_DRQ);
        else if (when_done == NOT_BSY_NOT_DRQ)
            result = !(status & ATA_CB_STAT_BSY) && !(status & ATA_CB_STAT_DRQ);
        else if (when_done == NOT_BSY_RDY)
            result = !(status & ATA_CB_STAT_BSY) && (status & ATA_CB_STAT_RDY);
        else if (when_done == TIMEOUT)
            result = 0;

        if (result)
            return 0;
        // mod 2048 each 16 ms
        if (time>>16 != last) {
            last = time >>16;
            DEBUGF("await_ide: (TIMEOUT,BSY,!BSY,!BSY_DRQ,!BSY_!DRQ,!BSY_RDY)"
                   " %d time= %d timeout= %d\n"
                   , when_done, time>>11, timeout);
        }
        if (status & ATA_CB_STAT_ERR) {
            DEBUGF("await_ide: ERROR (TIMEOUT,BSY,!BSY,!BSY_DRQ"
                   ",!BSY_!DRQ,!BSY_RDY) %d time= %d timeout= %d\n"
                   , when_done, time>>11, timeout);
            return -1;
        }
        if ((timeout == 0) || ((time>>11) > timeout))
            break;
    }
    BX_INFO("IDE time out\n");
    return -1;
}


// ---------------------------------------------------------------------------
// ATA/ATAPI driver : software reset
// ---------------------------------------------------------------------------
// ATA-3
// 8.2.1 Software reset - Device 0

void
ata_reset(u16 device)
{
    u16 iobase1, iobase2;
    u8  channel, slave, sn, sc;
    u8  type;

    channel = device / 2;
    slave = device % 2;

    iobase1 = GET_EBDA(ata.channels[channel].iobase1);
    iobase2 = GET_EBDA(ata.channels[channel].iobase2);

    // Reset

    // 8.2.1 (a) -- set SRST in DC
    outb(ATA_CB_DC_HD15 | ATA_CB_DC_NIEN | ATA_CB_DC_SRST, iobase2+ATA_CB_DC);

    // 8.2.1 (b) -- wait for BSY
    await_ide(BSY, iobase1, 20);

    // 8.2.1 (f) -- clear SRST
    outb(ATA_CB_DC_HD15 | ATA_CB_DC_NIEN, iobase2+ATA_CB_DC);

    type=GET_EBDA(ata.devices[device].type);
    if (type != ATA_TYPE_NONE) {

        // 8.2.1 (g) -- check for sc==sn==0x01
        // select device
        outb(slave?ATA_CB_DH_DEV1:ATA_CB_DH_DEV0, iobase1+ATA_CB_DH);
        sc = inb(iobase1+ATA_CB_SC);
        sn = inb(iobase1+ATA_CB_SN);

        if ( (sc==0x01) && (sn==0x01) ) {
            if (type == ATA_TYPE_ATA) //ATA
                await_ide(NOT_BSY_RDY, iobase1, IDE_TIMEOUT);
            else //ATAPI
                await_ide(NOT_BSY, iobase1, IDE_TIMEOUT);
        }

        // 8.2.1 (h) -- wait for not BSY
        await_ide(NOT_BSY, iobase1, IDE_TIMEOUT);
    }

    // Enable interrupts
    outb(ATA_CB_DC_HD15, iobase2+ATA_CB_DC);
}


// ---------------------------------------------------------------------------
// ATA/ATAPI driver : execute a data command
// ---------------------------------------------------------------------------
      // returns
      // 0 : no error
      // 1 : BUSY bit set
      // 2 : read error
      // 3 : expected DRQ=1
      // 4 : no sectors left to read/verify
      // 5 : more sectors to read/verify
      // 6 : no sectors left to write
      // 7 : more sectors to write

static int
send_cmd(struct ata_pio_command *cmd)
{
    u16 biosid = cmd->biosid;
    u8 channel = biosid / 2;
    u16 iobase1 = GET_EBDA(ata.channels[channel].iobase1);
    u16 iobase2 = GET_EBDA(ata.channels[channel].iobase2);

    u8 status = inb(iobase1 + ATA_CB_STAT);
    if (status & ATA_CB_STAT_BSY)
        return 1;

    outb(ATA_CB_DC_HD15 | ATA_CB_DC_NIEN, iobase2 + ATA_CB_DC);
    if (cmd->command & 0x04) {
        outb(0x00, iobase1 + ATA_CB_FR);
        outb(cmd->sector_count2, iobase1 + ATA_CB_SC);
        outb(cmd->lba_low2, iobase1 + ATA_CB_SN);
        outb(cmd->lba_mid2, iobase1 + ATA_CB_CL);
        outb(cmd->lba_high2, iobase1 + ATA_CB_CH);
    }
    outb(cmd->feature, iobase1 + ATA_CB_FR);
    outb(cmd->sector_count, iobase1 + ATA_CB_SC);
    outb(cmd->lba_low, iobase1 + ATA_CB_SN);
    outb(cmd->lba_mid, iobase1 + ATA_CB_CL);
    outb(cmd->lba_high, iobase1 + ATA_CB_CH);
    outb(cmd->device, iobase1 + ATA_CB_DH);
    outb(cmd->command, iobase1 + ATA_CB_CMD);

    await_ide(NOT_BSY_DRQ, iobase1, IDE_TIMEOUT);

    status = inb(iobase1 + ATA_CB_STAT);
    if (status & ATA_CB_STAT_ERR) {
        DEBUGF("send_cmd : read error\n");
        return 2;
    }
    if (!(status & ATA_CB_STAT_DRQ)) {
        DEBUGF("send_cmd : DRQ not set (status %02x)\n"
               , (unsigned) status);
        return 3;
    }

    return 0;
}

int
ata_transfer(struct ata_pio_command *cmd)
{
    DEBUGF("ata_transfer id=%d cmd=%d lba=%d count=%d buf=%p\n"
           , cmd->biosid, cmd->command
           , (cmd->lba_high << 16) | (cmd->lba_mid << 8) | cmd->lba_low
           , cmd->sector_count, cmd->far_buffer);

    // Reset count of transferred data
    SET_EBDA(ata.trsfsectors,0);
    SET_EBDA(ata.trsfbytes,0L);

    int ret = send_cmd(cmd);
    if (ret)
        return ret;

    u16 biosid = cmd->biosid;
    u8 channel  = biosid / 2;
    u16 iobase1 = GET_EBDA(ata.channels[channel].iobase1);
    u16 iobase2 = GET_EBDA(ata.channels[channel].iobase2);
    u8 mode     = GET_EBDA(ata.devices[biosid].mode);
    int iswrite = (cmd->command & ~0x40) == ATA_CMD_WRITE_SECTORS;

    irq_enable();

    u8 current = 0;
    u16 count = cmd->sector_count;
    u8 status;
    void *far_buffer = cmd->far_buffer;
    for (;;) {
        if (iswrite) {
            // Write data to controller
            DEBUGF("Write sector id=%d dest=%p\n", biosid, far_buffer);
            if (mode == ATA_MODE_PIO32)
                outsl_far(iobase1, far_buffer, 512 / 4);
            else
                outsw_far(iobase1, far_buffer, 512 / 2);
        } else {
            // Read data from controller
            DEBUGF("Read sector id=%d dest=%p\n", biosid, far_buffer);
            if (mode == ATA_MODE_PIO32)
                insl_far(iobase1, far_buffer, 512 / 4);
            else
                insw_far(iobase1, far_buffer, 512 / 2);
            await_ide(NOT_BSY, iobase1, IDE_TIMEOUT);
        }
        far_buffer += 512;

        current++;
        SET_EBDA(ata.trsfsectors,current);
        count--;
        status = inb(iobase1 + ATA_CB_STAT);
        if (count == 0)
            break;
        status &= (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ
                   | ATA_CB_STAT_ERR);
        if (status != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ)) {
            DEBUGF("ata_transfer : more sectors left (status %02x)\n"
                   , (unsigned) status);
            return 5;
        }
    }

    status &= (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DF
               | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR);
    if (!iswrite)
        status &= ~ATA_CB_STAT_DF;
    if (status != ATA_CB_STAT_RDY ) {
        DEBUGF("ata_transfer : no sectors left (status %02x)\n"
               , (unsigned) status);
        return 4;
    }

    irq_disable();

    // Enable interrupts
    outb(ATA_CB_DC_HD15, iobase2+ATA_CB_DC);
    return 0;
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : execute a packet command
// ---------------------------------------------------------------------------
      // returns
      // 0 : no error
      // 1 : error in parameters
      // 2 : BUSY bit set
      // 3 : error
      // 4 : not ready
int
ata_cmd_packet(u16 biosid, u8 *cmdbuf, u8 cmdlen
               , u16 header, u32 length, void *far_buffer)
{
    DEBUGF("ata_cmd_packet d=%d cmdlen=%d h=%d l=%d buf=%p\n"
           , biosid, cmdlen, header, length, far_buffer);

    u8 channel = biosid / 2;
    u8 slave = biosid % 2;

    // The header length must be even
    if (header & 1) {
        DEBUGF("ata_cmd_packet : header must be even (%04x)\n", header);
        return 1;
    }

    u16 iobase1 = GET_EBDA(ata.channels[channel].iobase1);
    u16 iobase2 = GET_EBDA(ata.channels[channel].iobase2);
    u8 mode     = GET_EBDA(ata.devices[biosid].mode);

    struct ata_pio_command cmd;
    cmd.sector_count = 0;
    cmd.feature = 0;
    cmd.lba_low = 0;
    cmd.lba_mid = 0xf0;
    cmd.lba_high = 0xff;
    cmd.device = slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0;
    cmd.command = ATA_CMD_PACKET;

    cmd.biosid = biosid;
    int ret = send_cmd(&cmd);
    if (ret)
        return ret;

    // Reset count of transferred data
    SET_EBDA(ata.trsfsectors,0);
    SET_EBDA(ata.trsfbytes,0L);

    irq_enable();

    // Send command to device
    outsw_far(iobase1, MAKE_32_PTR(GET_SEG(SS), (u32)cmdbuf), cmdlen / 2);

    u8 status;
    u16 loops = 0;
    for (;;) {
        if (loops == 0) {//first time through
            status = inb(iobase2 + ATA_CB_ASTAT);
            await_ide(NOT_BSY_DRQ, iobase1, IDE_TIMEOUT);
        } else
            await_ide(NOT_BSY, iobase1, IDE_TIMEOUT);
        loops++;

        status = inb(iobase1 + ATA_CB_STAT);
        inb(iobase1 + ATA_CB_SC);

        // Check if command completed
        if(((inb(iobase1 + ATA_CB_SC)&0x7)==0x3) &&
           ((status & (ATA_CB_STAT_RDY | ATA_CB_STAT_ERR)) == ATA_CB_STAT_RDY))
            break;

        if (status & ATA_CB_STAT_ERR) {
            DEBUGF("ata_cmd_packet : error (status %02x)\n", status);
            return 3;
        }

        // Get the byte count
        u16 lcount =  (((u16)(inb(iobase1 + ATA_CB_CH))<<8)
                       + inb(iobase1 + ATA_CB_CL));

        // adjust to read what we want
        u16 lbefore, lafter;
        if (header > lcount) {
            lbefore=lcount;
            header-=lcount;
            lcount=0;
        } else {
            lbefore=header;
            header=0;
            lcount-=lbefore;
        }

        if (lcount > length) {
            lafter=lcount-length;
            lcount=length;
            length=0;
        } else {
            lafter=0;
            length-=lcount;
        }

        // Save byte count
        u16 count = lcount;

        DEBUGF("Trying to read %04x bytes (%04x %04x %04x) to %p\n"
               , lbefore+lcount+lafter, lbefore, lcount, lafter, far_buffer);

        // If counts not dividable by 4, use 16bits mode
        u8 lmode = mode;
        if (lbefore & 0x03) lmode=ATA_MODE_PIO16;
        if (lcount  & 0x03) lmode=ATA_MODE_PIO16;
        if (lafter  & 0x03) lmode=ATA_MODE_PIO16;

        // adds an extra byte if count are odd. before is always even
        if (lcount & 0x01) {
            lcount+=1;
            if ((lafter > 0) && (lafter & 0x01)) {
                lafter-=1;
            }
        }

        if (lmode == ATA_MODE_PIO32) {
            lcount>>=2; lbefore>>=2; lafter>>=2;
        } else {
            lcount>>=1; lbefore>>=1; lafter>>=1;
        }

        int i;
        for (i=0; i<lbefore; i++)
            if (lmode == ATA_MODE_PIO32)
                inl(iobase1);
            else
                inw(iobase1);

        if (lmode == ATA_MODE_PIO32)
            insl_far(iobase1, far_buffer, lcount);
        else
            insw_far(iobase1, far_buffer, lcount);

        for (i=0; i<lafter; i++)
            if (lmode == ATA_MODE_PIO32)
                inl(iobase1);
            else
                inw(iobase1);

        // Compute new buffer address
        far_buffer += count;

        // Save transferred bytes count
        SET_EBDA(ata.trsfsectors, loops);
    }

    // Final check, device must be ready
    if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DF
                    | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) )
         != ATA_CB_STAT_RDY ) {
        DEBUGF("ata_cmd_packet : not ready (status %02x)\n"
               , (unsigned) status);
        return 4;
    }

    // Enable interrupts
    outb(ATA_CB_DC_HD15, iobase2+ATA_CB_DC);
    return 0;
}

int
cdrom_read(u16 biosid, u32 lba, u32 count, void *far_buffer, u16 skip)
{
    u16 sectors = (count + 2048 - 1) / 2048;

    u8 atacmd[12];
    memset(atacmd, 0, sizeof(atacmd));
    atacmd[0]=0x28;                      // READ command
    atacmd[7]=(sectors & 0xff00) >> 8;   // Sectors
    atacmd[8]=(sectors & 0x00ff);        // Sectors
    atacmd[2]=(lba & 0xff000000) >> 24;  // LBA
    atacmd[3]=(lba & 0x00ff0000) >> 16;
    atacmd[4]=(lba & 0x0000ff00) >> 8;
    atacmd[5]=(lba & 0x000000ff);

    return ata_cmd_packet(biosid, atacmd, sizeof(atacmd)
                          , skip, count, far_buffer);
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : device detection
// ---------------------------------------------------------------------------

void
ata_detect()
{
    u8  hdcount, cdcount, device, type;
    u8  buffer[0x0200];
    memset(buffer, 0, sizeof(buffer));

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

            u16 ret = ata_cmd_data_chs(device, ATA_CMD_IDENTIFY_DEVICE
                                       , 0, 0, 1, 1
                                       , MAKE_32_PTR(GET_SEG(SS), (u32)buffer));
            if (ret)
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
            BX_INFO("ata%d-%d: PCHS=%u/%d/%d translation="
                    , channel, slave,cylinders, heads, spt);

            translation = inb_cmos(CMOS_BIOS_DISKTRANSFLAG + channel/2);
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
            if (cylinders > 1024)
                cylinders=1024;
            BX_INFO(" LCHS=%d/%d/%d\n", cylinders, heads, spt);

            SET_EBDA(ata.devices[device].lchs.heads, heads);
            SET_EBDA(ata.devices[device].lchs.cylinders, cylinders);
            SET_EBDA(ata.devices[device].lchs.spt, spt);

            // fill hdidmap
            SET_EBDA(ata.idmap[0][hdcount], device);
            hdcount++;
        }

        // Now we send a IDENTIFY command to ATAPI device
        if(type == ATA_TYPE_ATAPI) {

            u8  type, removable, mode;
            u16 blksize;

            //Temporary values to do the transfer
            SET_EBDA(ata.devices[device].device,ATA_DEVICE_CDROM);
            SET_EBDA(ata.devices[device].mode, ATA_MODE_PIO16);

            u16 ret = ata_cmd_data_chs(device, ATA_CMD_IDENTIFY_DEVICE_PACKET
                                       , 0, 0, 1, 1
                                       , MAKE_32_PTR(GET_SEG(SS), (u32)buffer));
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
            SET_EBDA(ata.idmap[1][cdcount], device);
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
                printf(" ATA-%d Hard-Disk (%u MBytes)\n", version, (u16)sizeinmb);
            else
                printf(" ATA-%d Hard-Disk (%u GBytes)\n", version, (u16)(sizeinmb>>10));
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
