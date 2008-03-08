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
    u32 time=0,last=0;
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

static void
insw(u16 port, u16 segment, u16 offset, u16 count)
{
    u16 i;
    for (i=0; i<count; i++) {
        u16 d = inw(port);
        SET_FARVAR(segment, *(u16*)(offset + 2*i), d);
    }
}

static void
insl(u16 port, u16 segment, u16 offset, u16 count)
{
    u16 i;
    for (i=0; i<count; i++) {
        u32 d = inl(port);
        SET_FARVAR(segment, *(u32*)(offset + 4*i), d);
    }
}

static void
outsw(u16 port, u16 segment, u16 offset, u16 count)
{
    u16 i;
    for (i=0; i<count; i++) {
        u16 d = GET_FARVAR(segment, *(u16*)(offset + 2*i));
        outw(d, port);
    }
}

static void
outsl(u16 port, u16 segment, u16 offset, u16 count)
{
    u16 i;
    for (i=0; i<count; i++) {
        u32 d = GET_FARVAR(segment, *(u32*)(offset + 4*i));
        outl(d, port);
    }
}


// ---------------------------------------------------------------------------
// ATA/ATAPI driver : execute a data-in command
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
u16
ata_cmd_data_in(u16 device, u16 command, u16 count, u16 cylinder
                , u16 head, u16 sector, u32 lba, u16 segment, u16 offset)
{
    u16 iobase1, iobase2, blksize;
    u8  channel, slave;
    u8  status, current, mode;

    channel = device / 2;
    slave   = device % 2;

    iobase1 = GET_EBDA(ata.channels[channel].iobase1);
    iobase2 = GET_EBDA(ata.channels[channel].iobase2);
    mode    = GET_EBDA(ata.devices[device].mode);
    blksize = 0x200;
    if (mode == ATA_MODE_PIO32)
        blksize>>=2;
    else
        blksize>>=1;

    // Reset count of transferred data
    SET_EBDA(ata.trsfsectors,0);
    SET_EBDA(ata.trsfbytes,0L);
    current = 0;

    status = inb(iobase1 + ATA_CB_STAT);
    if (status & ATA_CB_STAT_BSY)
        return 1;

    outb(ATA_CB_DC_HD15 | ATA_CB_DC_NIEN, iobase2 + ATA_CB_DC);

    // sector will be 0 only on lba access. Convert to lba-chs
    if (sector == 0) {
        if ((count >= 1 << 8) || (lba + count >= 1UL << 28)) {
            outb(0x00, iobase1 + ATA_CB_FR);
            outb((count >> 8) & 0xff, iobase1 + ATA_CB_SC);
            outb(lba >> 24, iobase1 + ATA_CB_SN);
            outb(0, iobase1 + ATA_CB_CL);
            outb(0, iobase1 + ATA_CB_CH);
            command |= 0x04;
            count &= (1UL << 8) - 1;
            lba &= (1UL << 24) - 1;
        }
        sector = (u16) (lba & 0x000000ffL);
        cylinder = (u16) ((lba>>8) & 0x0000ffffL);
        head = ((u16) ((lba>>24) & 0x0000000fL)) | ATA_CB_DH_LBA;
    }

    outb(0x00, iobase1 + ATA_CB_FR);
    outb(count, iobase1 + ATA_CB_SC);
    outb(sector, iobase1 + ATA_CB_SN);
    outb(cylinder & 0x00ff, iobase1 + ATA_CB_CL);
    outb(cylinder >> 8, iobase1 + ATA_CB_CH);
    outb((slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0) | (u8) head
         , iobase1 + ATA_CB_DH);
    outb(command, iobase1 + ATA_CB_CMD);

    await_ide(NOT_BSY_DRQ, iobase1, IDE_TIMEOUT);
    status = inb(iobase1 + ATA_CB_STAT);

    if (status & ATA_CB_STAT_ERR) {
        DEBUGF("ata_cmd_data_in : read error\n");
        return 2;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
        DEBUGF("ata_cmd_data_in : DRQ not set (status %02x)\n"
               , (unsigned) status);
        return 3;
    }

    // FIXME : move seg/off translation here

    irq_enable();

    while (1) {

        if (offset > 0xf800) {
            offset -= 0x800;
            segment += 0x80;
        }

        if (mode == ATA_MODE_PIO32)
            insl(iobase1, segment, offset, blksize);
        else
            insw(iobase1, segment, offset, blksize);

        current++;
        SET_EBDA(ata.trsfsectors,current);
        count--;
        await_ide(NOT_BSY, iobase1, IDE_TIMEOUT);
        status = inb(iobase1 + ATA_CB_STAT);
        if (count == 0) {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ
                            | ATA_CB_STAT_ERR) )
                 != ATA_CB_STAT_RDY ) {
                DEBUGF("ata_cmd_data_in : no sectors left (status %02x)\n"
                       , (unsigned) status);
                return 4;
            }
            break;
        }
        else {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ
                            | ATA_CB_STAT_ERR) )
                 != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ) ) {
                DEBUGF("ata_cmd_data_in : more sectors left (status %02x)\n"
                       , (unsigned) status);
                return 5;
            }
            continue;
        }
    }
    // Enable interrupts
    outb(ATA_CB_DC_HD15, iobase2+ATA_CB_DC);
    return 0;
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : execute a data-out command
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
u16
ata_cmd_data_out(u16 device, u16 command, u16 count, u16 cylinder
                 , u16 head, u16 sector, u32 lba, u16 segment, u16 offset)
{
    u16 iobase1, iobase2, blksize;
    u8  channel, slave;
    u8  status, current, mode;

    channel = device / 2;
    slave   = device % 2;

    iobase1 = GET_EBDA(ata.channels[channel].iobase1);
    iobase2 = GET_EBDA(ata.channels[channel].iobase2);
    mode    = GET_EBDA(ata.devices[device].mode);
    blksize = 0x200;
    if (mode == ATA_MODE_PIO32)
        blksize>>=2;
    else
        blksize>>=1;

    // Reset count of transferred data
    SET_EBDA(ata.trsfsectors,0);
    SET_EBDA(ata.trsfbytes,0L);
    current = 0;

    status = inb(iobase1 + ATA_CB_STAT);
    if (status & ATA_CB_STAT_BSY)
        return 1;

    outb(ATA_CB_DC_HD15 | ATA_CB_DC_NIEN, iobase2 + ATA_CB_DC);

    // sector will be 0 only on lba access. Convert to lba-chs
    if (sector == 0) {
        if ((count >= 1 << 8) || (lba + count >= 1UL << 28)) {
            outb(0x00, iobase1 + ATA_CB_FR);
            outb((count >> 8) & 0xff, iobase1 + ATA_CB_SC);
            outb(lba >> 24, iobase1 + ATA_CB_SN);
            outb(0, iobase1 + ATA_CB_CL);
            outb(0, iobase1 + ATA_CB_CH);
            command |= 0x04;
            count &= (1UL << 8) - 1;
            lba &= (1UL << 24) - 1;
        }
        sector = (u16) (lba & 0x000000ffL);
        cylinder = (u16) ((lba>>8) & 0x0000ffffL);
        head = ((u16) ((lba>>24) & 0x0000000fL)) | ATA_CB_DH_LBA;
    }

    outb(0x00, iobase1 + ATA_CB_FR);
    outb(count, iobase1 + ATA_CB_SC);
    outb(sector, iobase1 + ATA_CB_SN);
    outb(cylinder & 0x00ff, iobase1 + ATA_CB_CL);
    outb(cylinder >> 8, iobase1 + ATA_CB_CH);
    outb((slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0) | (u8) head
         , iobase1 + ATA_CB_DH);
    outb(command, iobase1 + ATA_CB_CMD);

    await_ide(NOT_BSY_DRQ, iobase1, IDE_TIMEOUT);
    status = inb(iobase1 + ATA_CB_STAT);

    if (status & ATA_CB_STAT_ERR) {
        DEBUGF("ata_cmd_data_out : read error\n");
        return 2;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
        DEBUGF("ata_cmd_data_out : DRQ not set (status %02x)\n"
               , (unsigned) status);
        return 3;
    }

    // FIXME : move seg/off translation here

    irq_enable();

    while (1) {

        if (offset > 0xf800) {
            offset -= 0x800;
            segment += 0x80;
        }

        if (mode == ATA_MODE_PIO32)
            outsl(iobase1, segment, offset, blksize);
        else
            outsw(iobase1, segment, offset, blksize);

        current++;
        SET_EBDA(ata.trsfsectors,current);
        count--;
        status = inb(iobase1 + ATA_CB_STAT);
        if (count == 0) {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DF
                            | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) )
                 != ATA_CB_STAT_RDY ) {
                DEBUGF("ata_cmd_data_out : no sectors left (status %02x)\n"
                       , (unsigned) status);
                return 6;
            }
            break;
        } else {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ
                            | ATA_CB_STAT_ERR) )
                 != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ) ) {
                DEBUGF("ata_cmd_data_out : more sectors left (status %02x)\n"
                       , (unsigned) status);
                return 7;
            }
            continue;
        }
    }
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
u16
ata_cmd_packet(u16 device, u8 *cmdbuf, u8 cmdlen, u16 header
               , u32 length, u8 inout, u16 bufseg, u16 bufoff)
{
    u16 iobase1, iobase2;
    u16 lcount, lbefore, lafter, count;
    u8  channel, slave;
    u8  status, mode, lmode;
    u32 transfer;

    channel = device / 2;
    slave = device % 2;

    // Data out is not supported yet
    if (inout == ATA_DATA_OUT) {
        BX_INFO("ata_cmd_packet: DATA_OUT not supported yet\n");
        return 1;
    }

    // The header length must be even
    if (header & 1) {
        DEBUGF("ata_cmd_packet : header must be even (%04x)\n", header);
        return 1;
    }

    iobase1 = GET_EBDA(ata.channels[channel].iobase1);
    iobase2 = GET_EBDA(ata.channels[channel].iobase2);
    mode    = GET_EBDA(ata.devices[device].mode);
    transfer= 0L;

    if (cmdlen < 12)
        cmdlen=12;
    if (cmdlen > 12)
        cmdlen=16;
    cmdlen>>=1;

    // Reset count of transferred data
    SET_EBDA(ata.trsfsectors,0);
    SET_EBDA(ata.trsfbytes,0L);

    status = inb(iobase1 + ATA_CB_STAT);
    if (status & ATA_CB_STAT_BSY)
        return 2;

    outb(ATA_CB_DC_HD15 | ATA_CB_DC_NIEN, iobase2 + ATA_CB_DC);
    outb(0x00, iobase1 + ATA_CB_FR);
    outb(0x00, iobase1 + ATA_CB_SC);
    outb(0x00, iobase1 + ATA_CB_SN);
    outb(0xfff0 & 0x00ff, iobase1 + ATA_CB_CL);
    outb(0xfff0 >> 8, iobase1 + ATA_CB_CH);
    outb(slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0, iobase1 + ATA_CB_DH);
    outb(ATA_CMD_PACKET, iobase1 + ATA_CB_CMD);

    // Device should ok to receive command
    await_ide(NOT_BSY_DRQ, iobase1, IDE_TIMEOUT);
    status = inb(iobase1 + ATA_CB_STAT);

    if (status & ATA_CB_STAT_ERR) {
        DEBUGF("ata_cmd_packet : error, status is %02x\n", status);
        return 3;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
        DEBUGF("ata_cmd_packet : DRQ not set (status %02x)\n"
               , (unsigned) status);
        return 4;
    }

    // Send command to device
    irq_enable();

    outsw(iobase1, GET_SEG(SS), (u32)cmdbuf, cmdlen);

    if (inout == ATA_DATA_NO) {
        await_ide(NOT_BSY, iobase1, IDE_TIMEOUT);
        status = inb(iobase1 + ATA_CB_STAT);
    } else {
        u16 loops = 0;
        u8 sc;
        while (1) {

            if (loops == 0) {//first time through
                status = inb(iobase2 + ATA_CB_ASTAT);
                await_ide(NOT_BSY_DRQ, iobase1, IDE_TIMEOUT);
            } else
                await_ide(NOT_BSY, iobase1, IDE_TIMEOUT);
            loops++;

            status = inb(iobase1 + ATA_CB_STAT);
            sc = inb(iobase1 + ATA_CB_SC);

            // Check if command completed
            if(((inb(iobase1 + ATA_CB_SC)&0x7)==0x3) &&
               ((status & (ATA_CB_STAT_RDY | ATA_CB_STAT_ERR)) == ATA_CB_STAT_RDY))
                break;

            if (status & ATA_CB_STAT_ERR) {
                DEBUGF("ata_cmd_packet : error (status %02x)\n", status);
                return 3;
            }

            // Normalize address
            bufseg += (bufoff / 16);
            bufoff %= 16;

            // Get the byte count
            lcount =  ((u16)(inb(iobase1 + ATA_CB_CH))<<8)+inb(iobase1 + ATA_CB_CL);

            // adjust to read what we want
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
            count = lcount;

            DEBUGF("Trying to read %04x bytes (%04x %04x %04x) "
                   , lbefore+lcount+lafter, lbefore, lcount, lafter);
            DEBUGF("to 0x%04x:0x%04x\n", bufseg, bufoff);

            // If counts not dividable by 4, use 16bits mode
            lmode = mode;
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
                insl(iobase1, bufseg, bufoff, lcount);
            else
                insw(iobase1, bufseg, bufoff, lcount);

            for (i=0; i<lafter; i++)
                if (lmode == ATA_MODE_PIO32)
                    inl(iobase1);
                else
                    inw(iobase1);

            // Compute new buffer address
            bufoff += count;

            // Save transferred bytes count
            transfer += count;
            SET_EBDA(ata.trsfbytes,transfer);
        }
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

u16
cdrom_read(u16 device, u32 lba, u32 count, u16 segment, u16 offset, u16 skip)
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

    return ata_cmd_packet(device, atacmd, sizeof(atacmd)
                          , skip, count, ATA_DATA_IN
                          , segment, offset);
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

            u16 ret = ata_cmd_data_in(device,ATA_CMD_IDENTIFY_DEVICE
                                      , 1, 0, 0, 0, 0L
                                      , GET_SEG(SS), (u32)buffer);
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
            BX_INFO("ata%d-%d: PCHS=%u/%d/%d translation=", channel, slave,cylinders, heads, spt);

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
