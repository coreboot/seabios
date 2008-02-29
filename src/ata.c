#include "ata.h" // ATA_*
#include "types.h" // u8
#include "ioport.h" // inb
#include "util.h" // BX_INFO

#define TIMEOUT 0
#define BSY 1
#define NOT_BSY 2
#define NOT_BSY_DRQ 3
#define NOT_BSY_NOT_DRQ 4
#define NOT_BSY_RDY 5

#define IDE_TIMEOUT 32000u //32 seconds max for IDE ops

#define BX_DEBUG_ATA BX_INFO

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
            BX_DEBUG_ATA("await_ide: (TIMEOUT,BSY,!BSY,!BSY_DRQ,!BSY_!DRQ,!BSY_RDY) %d time= %d timeout= %d\n",when_done,time>>11, timeout);
        }
        if (status & ATA_CB_STAT_ERR) {
            BX_DEBUG_ATA("await_ide: ERROR (TIMEOUT,BSY,!BSY,!BSY_DRQ,!BSY_!DRQ,!BSY_RDY) %d time= %d timeout= %d\n",when_done,time>>11, timeout);
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
        SET_FARVAR(segment, *(u16*)(offset + i), d);
    }
}

static void
insl(u16 port, u16 segment, u16 offset, u16 count)
{
    u16 i;
    for (i=0; i<count; i++) {
        u32 d = inl(port);
        SET_FARVAR(segment, *(u32*)(offset + i), d);
    }
}

static void
outsw(u16 port, u16 segment, u16 offset, u16 count)
{
    u16 i;
    for (i=0; i<count; i++) {
        u16 d = GET_FARVAR(segment, *(u16*)(offset + i));
        outw(d, port);
    }
}

static void
outsl(u16 port, u16 segment, u16 offset, u16 count)
{
    u16 i;
    for (i=0; i<count; i++) {
        u32 d = GET_FARVAR(segment, *(u32*)(offset + i));
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
    if (mode == ATA_MODE_PIO32) blksize>>=2;
    else blksize>>=1;

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
        BX_DEBUG_ATA("ata_cmd_data_in : read error\n");
        return 2;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
        BX_DEBUG_ATA("ata_cmd_data_in : DRQ not set (status %02x)\n"
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
            insw(iobase1, segment, offset, blksize);
        else
            insl(iobase1, segment, offset, blksize);

        current++;
        SET_EBDA(ata.trsfsectors,current);
        count--;
        await_ide(NOT_BSY, iobase1, IDE_TIMEOUT);
        status = inb(iobase1 + ATA_CB_STAT);
        if (count == 0) {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ
                            | ATA_CB_STAT_ERR) )
                 != ATA_CB_STAT_RDY ) {
                BX_DEBUG_ATA("ata_cmd_data_in : no sectors left (status %02x)\n"
                             , (unsigned) status);
                return 4;
            }
            break;
        }
        else {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ
                            | ATA_CB_STAT_ERR) )
                 != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ) ) {
                BX_DEBUG_ATA("ata_cmd_data_in : more sectors left (status %02x)\n"
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
        BX_DEBUG_ATA("ata_cmd_data_out : read error\n");
        return 2;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
        BX_DEBUG_ATA("ata_cmd_data_out : DRQ not set (status %02x)\n"
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
            outsw(iobase1, segment, offset, blksize);
        else
            outsl(iobase1, segment, offset, blksize);

        current++;
        SET_EBDA(ata.trsfsectors,current);
        count--;
        status = inb(iobase1 + ATA_CB_STAT);
        if (count == 0) {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DF
                            | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) )
                 != ATA_CB_STAT_RDY ) {
                BX_DEBUG_ATA("ata_cmd_data_out : no sectors left (status %02x)\n"
                             , (unsigned) status);
                return 6;
            }
            break;
        } else {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ
                            | ATA_CB_STAT_ERR) )
                 != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ) ) {
                BX_DEBUG_ATA("ata_cmd_data_out : more sectors left (status %02x)\n"
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
ata_cmd_packet(u16 device, u8 cmdlen, u16 cmdseg, u16 cmdoff, u16 header
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
        BX_DEBUG_ATA("ata_cmd_packet : header must be even (%04x)\n",header);
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
        BX_DEBUG_ATA("ata_cmd_packet : error, status is %02x\n",status);
        return 3;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
        BX_DEBUG_ATA("ata_cmd_packet : DRQ not set (status %02x)\n"
                     , (unsigned) status);
        return 4;
    }

    // Normalize address
    cmdseg += (cmdoff / 16);
    cmdoff %= 16;

    // Send command to device
    irq_enable();

    outsw(iobase1, cmdseg, cmdoff, cmdlen);

    if (inout == ATA_DATA_NO) {
        await_ide(NOT_BSY, iobase1, IDE_TIMEOUT);
        status = inb(iobase1 + ATA_CB_STAT);
    }
    else {
        u16 loops = 0;
        u8 sc;
        while (1) {

            if (loops == 0) {//first time through
                status = inb(iobase2 + ATA_CB_ASTAT);
                await_ide(NOT_BSY_DRQ, iobase1, IDE_TIMEOUT);
            }
            else
                await_ide(NOT_BSY, iobase1, IDE_TIMEOUT);
            loops++;

            status = inb(iobase1 + ATA_CB_STAT);
            sc = inb(iobase1 + ATA_CB_SC);

            // Check if command completed
            if(((inb(iobase1 + ATA_CB_SC)&0x7)==0x3) &&
               ((status & (ATA_CB_STAT_RDY | ATA_CB_STAT_ERR)) == ATA_CB_STAT_RDY))
                break;

            if (status & ATA_CB_STAT_ERR) {
                BX_DEBUG_ATA("ata_cmd_packet : error (status %02x)\n",status);
                return 3;
            }

            // Normalize address
            bufseg += (bufoff / 16);
            bufoff %= 16;

            // Get the byte count
            lcount =  ((u16)(inb(iobase1 + ATA_CB_CH))<<8)+inb(iobase1 + ATA_CB_CL);

            // adjust to read what we want
            if(header>lcount) {
                lbefore=lcount;
                header-=lcount;
                lcount=0;
            }
            else {
                lbefore=header;
                header=0;
                lcount-=lbefore;
            }

            if(lcount>length) {
                lafter=lcount-length;
                lcount=length;
                length=0;
            }
            else {
                lafter=0;
                length-=lcount;
            }

            // Save byte count
            count = lcount;

            BX_DEBUG_ATA("Trying to read %04x bytes (%04x %04x %04x) "
                         ,lbefore+lcount+lafter,lbefore,lcount,lafter);
            BX_DEBUG_ATA("to 0x%04x:0x%04x\n",bufseg,bufoff);

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
            }
            else {
                lcount>>=1; lbefore>>=1; lafter>>=1;
            }

            int i;
            for (i=0; i<lbefore; i++)
                if (lmode == ATA_MODE_PIO32)
                    inl(iobase1);
                else
                    inw(iobase1);

            if (lmode == ATA_MODE_PIO32)
                insl(iobase1, bufoff, bufseg, lcount);
            else
                insw(iobase1, bufoff, bufseg, lcount);

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
        BX_DEBUG_ATA("ata_cmd_packet : not ready (status %02x)\n"
                     , (unsigned) status);
        return 4;
    }

    // Enable interrupts
    outb(ATA_CB_DC_HD15, iobase2+ATA_CB_DC);
    return 0;
}
