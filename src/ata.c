// Low level ATA disk access
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "ata.h" // ATA_*
#include "types.h" // u8
#include "ioport.h" // inb
#include "util.h" // dprintf
#include "cmos.h" // inb_cmos
#include "pic.h" // unmask_pic2
#include "biosvar.h" // GET_EBDA
#include "pci.h" // pci_find_class
#include "pci_ids.h" // PCI_CLASS_STORAGE_OTHER
#include "pci_regs.h" // PCI_INTERRUPT_LINE

#define TIMEOUT 0
#define BSY 1
#define NOT_BSY 2
#define NOT_BSY_DRQ 3
#define NOT_BSY_NOT_DRQ 4
#define NOT_BSY_RDY 5

#define IDE_SECTOR_SIZE 512
#define CDROM_SECTOR_SIZE 2048

#define IDE_TIMEOUT 32000u //32 seconds max for IDE ops


/****************************************************************
 * Helper functions
 ****************************************************************/

// Wait for the specified ide state
static int
await_ide(u8 when_done, u16 base, u16 timeout)
{
    u32 time=0, last=0;
    for (;;) {
        u8 status = inb(base+ATA_CB_STAT);
        time++;
        u8 result = 0;
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

        if (result)
            return status;
        // mod 2048 each 16 ms
        if (time>>16 != last) {
            last = time >>16;
            dprintf(6, "await_ide: (TIMEOUT,BSY,!BSY,!BSY_DRQ"
                    ",!BSY_!DRQ,!BSY_RDY) %d time= %d timeout= %d\n"
                    , when_done, time>>11, timeout);
        }
        if (status & ATA_CB_STAT_ERR) {
            dprintf(1, "await_ide: ERROR (TIMEOUT,BSY,!BSY,!BSY_DRQ"
                    ",!BSY_!DRQ,!BSY_RDY) %d status=%x time= %d timeout= %d\n"
                    , when_done, status, time>>11, timeout);
            return -1;
        }
        if (timeout == 0 || (time>>11) > timeout)
            break;
    }
    dprintf(1, "IDE time out\n");
    return -2;
}

// Wait for ide state - pauses for one ata cycle first.
static __always_inline int
pause_await_ide(u8 when_done, u16 iobase1, u16 iobase2, u16 timeout)
{
    // Wait one PIO transfer cycle.
    inb(iobase2 + ATA_CB_ASTAT);

    return await_ide(when_done, iobase1, timeout);
}

// Delay for x nanoseconds
static void
nsleep(u32 delay)
{
    // XXX - how to implement ndelay?
    while (delay--)
        nop();
}

// Wait for ide state - pause for 400ns first.
static __always_inline int
ndelay_await_ide(u8 when_done, u16 iobase1, u16 timeout)
{
    nsleep(400);
    return await_ide(when_done, iobase1, timeout);
}

// Delay for x milliseconds
static void
msleep(u32 delay)
{
    usleep(delay * 1000);
}

// Reset a drive
void
ata_reset(int driveid)
{
    u8 channel = driveid / 2;
    u8 slave = driveid % 2;
    u16 iobase1 = GET_EBDA(ata.channels[channel].iobase1);
    u16 iobase2 = GET_EBDA(ata.channels[channel].iobase2);

    // Reset

    // 8.2.1 (a) -- set SRST in DC
    outb(ATA_CB_DC_HD15 | ATA_CB_DC_NIEN | ATA_CB_DC_SRST, iobase2+ATA_CB_DC);

    // 8.2.1 (b) -- wait for BSY
    int status = await_ide(BSY, iobase1, 20);
    dprintf(6, "ata_reset(1) status=%x\n", status);

    // 8.2.1 (f) -- clear SRST
    outb(ATA_CB_DC_HD15 | ATA_CB_DC_NIEN, iobase2+ATA_CB_DC);

    // 8.2.1 (g) -- check for sc==sn==0x01
    // select device
    outb(slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0, iobase1+ATA_CB_DH);
    msleep(50);
    u8 sc = inb(iobase1+ATA_CB_SC);
    u8 sn = inb(iobase1+ATA_CB_SN);

    // For predetermined ATA drives - wait for ready.
    if (sc==0x01 && sn==0x01) {
        u8 type=GET_EBDA(ata.devices[driveid].type);
        if (type == ATA_TYPE_ATA)
            await_ide(NOT_BSY_RDY, iobase1, IDE_TIMEOUT);
    }

    // 8.2.1 (h) -- wait for not BSY
    status = await_ide(NOT_BSY, iobase1, IDE_TIMEOUT);
    dprintf(6, "ata_reset(2) status=%x\n", status);

    // Enable interrupts
    outb(ATA_CB_DC_HD15, iobase2+ATA_CB_DC);
}


/****************************************************************
 * ATA send command
 ****************************************************************/

struct ata_op_s {
    u64 lba;
    void *far_buffer;
    u16 driveid;
    u16 count;
};

struct ata_pio_command {
    u8 feature;
    u8 sector_count;
    u8 lba_low;
    u8 lba_mid;
    u8 lba_high;
    u8 device;
    u8 command;

    u8 sector_count2;
    u8 lba_low2;
    u8 lba_mid2;
    u8 lba_high2;
};

// Send an ata command to the drive.
static int
send_cmd(int driveid, struct ata_pio_command *cmd)
{
    u8 channel = driveid / 2;
    u16 iobase1 = GET_EBDA(ata.channels[channel].iobase1);
    u16 iobase2 = GET_EBDA(ata.channels[channel].iobase2);

    int status = inb(iobase1 + ATA_CB_STAT);
    if (status & ATA_CB_STAT_BSY)
        return -3;

    // Disable interrupts
    outb(ATA_CB_DC_HD15 | ATA_CB_DC_NIEN, iobase2 + ATA_CB_DC);

    // Select device
    u8 device = inb(iobase1 + ATA_CB_DH);
    outb(cmd->device, iobase1 + ATA_CB_DH);
    if ((device ^ cmd->device) & (1 << 4))
        // Wait for device to become active.
        msleep(50);

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
    outb(cmd->command, iobase1 + ATA_CB_CMD);

    status = ndelay_await_ide(NOT_BSY_DRQ, iobase1, IDE_TIMEOUT);
    if (status < 0)
        return status;

    if (status & ATA_CB_STAT_ERR) {
        dprintf(6, "send_cmd : read error\n");
        return -4;
    }
    if (!(status & ATA_CB_STAT_DRQ)) {
        dprintf(6, "send_cmd : DRQ not set (status %02x)\n"
                , (unsigned) status);
        return -5;
    }

    return 0;
}


/****************************************************************
 * ATA transfers
 ****************************************************************/

// Read and discard x number of bytes from an io channel.
static void
insx_discard(int mode, int iobase1, int bytes)
{
    int count, i;
    if (mode == ATA_MODE_PIO32) {
        count = bytes / 4;
        for (i=0; i<count; i++)
            inl(iobase1);
    } else {
        count = bytes / 2;
        for (i=0; i<count; i++)
            inw(iobase1);
    }
}

// Transfer 'count' blocks (of 'blocksize' bytes) to/from drive
// 'driveid'.  If 'skipfirst' or 'skiplast' is set then the first
// and/or last block may be partially transferred.  This function is
// inlined because all the callers use different forms and because the
// large number of parameters would consume a lot of stack space.
static __always_inline int
ata_transfer(int driveid, int iswrite, int count, int blocksize
             , int skipfirst, int skiplast, void *far_buffer)
{
    dprintf(16, "ata_transfer id=%d write=%d count=%d bs=%d"
            " skipf=%d skipl=%d buf=%p\n"
            , driveid, iswrite, count, blocksize
            , skipfirst, skiplast, far_buffer);

    // Reset count of transferred data
    SET_EBDA(ata.trsfsectors, 0);

    u8 channel  = driveid / 2;
    u16 iobase1 = GET_EBDA(ata.channels[channel].iobase1);
    u16 iobase2 = GET_EBDA(ata.channels[channel].iobase2);
    u8 mode     = GET_EBDA(ata.devices[driveid].mode);
    int current = 0;
    int status;
    for (;;) {
        int bsize = blocksize;
        if (skipfirst && current == 0) {
            insx_discard(mode, iobase1, skipfirst);
            bsize -= skipfirst;
        }
        if (skiplast && current == count-1)
            bsize -= skiplast;

        if (iswrite) {
            // Write data to controller
            dprintf(16, "Write sector id=%d dest=%p\n", driveid, far_buffer);
            if (mode == ATA_MODE_PIO32)
                outsl_far(iobase1, far_buffer, bsize / 4);
            else
                outsw_far(iobase1, far_buffer, bsize / 2);
        } else {
            // Read data from controller
            dprintf(16, "Read sector id=%d dest=%p\n", driveid, far_buffer);
            if (mode == ATA_MODE_PIO32)
                insl_far(iobase1, far_buffer, bsize / 4);
            else
                insw_far(iobase1, far_buffer, bsize / 2);
        }
        far_buffer += bsize;

        if (skiplast && current == count-1)
            insx_discard(mode, iobase1, skiplast);

        status = pause_await_ide(NOT_BSY, iobase1, iobase2, IDE_TIMEOUT);
        if (status < 0)
            // Error
            return status;

        current++;
        SET_EBDA(ata.trsfsectors, current);
        if (current == count)
            break;
        status &= (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ
                   | ATA_CB_STAT_ERR);
        if (status != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ)) {
            dprintf(6, "ata_transfer : more sectors left (status %02x)\n"
                    , (unsigned) status);
            return -6;
        }
    }

    status &= (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DF
               | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR);
    if (!iswrite)
        status &= ~ATA_CB_STAT_DF;
    if (status != ATA_CB_STAT_RDY ) {
        dprintf(6, "ata_transfer : no sectors left (status %02x)\n"
                , (unsigned) status);
        return -7;
    }

    // Enable interrupts
    outb(ATA_CB_DC_HD15, iobase2+ATA_CB_DC);
    return 0;
}

static noinline int
ata_transfer_disk(const struct ata_op_s *op, int iswrite)
{
    return ata_transfer(op->driveid, iswrite, op->count, IDE_SECTOR_SIZE
                        , 0, 0, op->far_buffer);
}

static noinline int
ata_transfer_cdrom(const struct ata_op_s *op)
{
    return ata_transfer(op->driveid, 0, op->count, CDROM_SECTOR_SIZE
                        , 0, 0, op->far_buffer);
}

static noinline int
ata_transfer_emu(const struct ata_op_s *op, int before, int after)
{
    int vcount = op->count * 4 - before - after;
    int ret = ata_transfer(op->driveid, 0, op->count, CDROM_SECTOR_SIZE
                           , before*512, after*512, op->far_buffer);
    if (ret) {
        SET_EBDA(ata.trsfsectors, 0);
        return ret;
    }
    SET_EBDA(ata.trsfsectors, vcount);
    return 0;
}


/****************************************************************
 * ATA hard drive functions
 ****************************************************************/

static noinline int
send_cmd_disk(const struct ata_op_s *op, u16 command)
{
    u8 slave = op->driveid % 2;
    u64 lba = op->lba;

    struct ata_pio_command cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.command = command;
    if (op->count >= (1<<8) || lba + op->count >= (1<<28)) {
        cmd.sector_count2 = op->count >> 8;
        cmd.lba_low2 = lba >> 24;
        cmd.lba_mid2 = lba >> 32;
        cmd.lba_high2 = lba >> 40;

        cmd.command |= 0x04;
        lba &= 0xffffff;
    }

    cmd.feature = 0;
    cmd.sector_count = op->count;
    cmd.lba_low = lba;
    cmd.lba_mid = lba >> 8;
    cmd.lba_high = lba >> 16;
    cmd.device = ((slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0)
                  | ((lba >> 24) & 0xf) | ATA_CB_DH_LBA);

    return send_cmd(op->driveid, &cmd);
}

// Read/write count blocks from a harddrive.
__always_inline int
ata_cmd_data(int driveid, u16 command, u64 lba, u16 count, void *far_buffer)
{
    struct ata_op_s op;
    op.driveid = driveid;
    op.lba = lba;
    op.count = count;
    op.far_buffer = far_buffer;

    int ret = send_cmd_disk(&op, command);
    if (ret)
        return ret;

    int iswrite = command == ATA_CMD_WRITE_SECTORS;
    return ata_transfer_disk(&op, iswrite);
}


/****************************************************************
 * ATAPI functions
 ****************************************************************/

// Low-level atapi command transmit function.
static __always_inline int
send_atapi_cmd(int driveid, u8 *cmdbuf, u8 cmdlen, u16 blocksize)
{
    u8 channel = driveid / 2;
    u8 slave = driveid % 2;
    u16 iobase1 = GET_EBDA(ata.channels[channel].iobase1);
    u16 iobase2 = GET_EBDA(ata.channels[channel].iobase2);

    struct ata_pio_command cmd;
    cmd.sector_count = 0;
    cmd.feature = 0;
    cmd.lba_low = 0;
    cmd.lba_mid = blocksize;
    cmd.lba_high = blocksize >> 8;
    cmd.device = slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0;
    cmd.command = ATA_CMD_PACKET;

    int ret = send_cmd(driveid, &cmd);
    if (ret)
        return ret;

    // Send command to device
    outsw_far(iobase1, MAKE_FARPTR(GET_SEG(SS), (u32)cmdbuf), cmdlen / 2);

    int status = pause_await_ide(NOT_BSY_DRQ, iobase1, iobase2, IDE_TIMEOUT);
    if (status < 0)
        return status;

    return 0;
}

// Low-level cdrom read atapi command transmit function.
static int
send_cmd_cdrom(const struct ata_op_s *op)
{
    u8 atacmd[12];
    memset(atacmd, 0, sizeof(atacmd));

    atacmd[0]=0x28;                         // READ command
    atacmd[7]=(op->count & 0xff00) >> 8;    // Sectors
    atacmd[8]=(op->count & 0x00ff);
    atacmd[2]=(op->lba & 0xff000000) >> 24; // LBA
    atacmd[3]=(op->lba & 0x00ff0000) >> 16;
    atacmd[4]=(op->lba & 0x0000ff00) >> 8;
    atacmd[5]=(op->lba & 0x000000ff);

    return send_atapi_cmd(op->driveid, atacmd, sizeof(atacmd)
                          , CDROM_SECTOR_SIZE);
}

// Read sectors from the cdrom.
__always_inline int
cdrom_read(int driveid, u32 lba, u32 count, void *far_buffer)
{
    struct ata_op_s op;
    op.driveid = driveid;
    op.lba = lba;
    op.count = count;
    op.far_buffer = far_buffer;

    int ret = send_cmd_cdrom(&op);
    if (ret)
        return ret;

    return ata_transfer_cdrom(&op);
}

// Pretend the cdrom has 512 byte sectors (instead of 2048) and read
// sectors.
__always_inline int
cdrom_read_512(int driveid, u32 vlba, u32 vcount, void *far_buffer)
{
    u32 velba = vlba + vcount - 1;
    u32 lba = vlba / 4;
    u32 elba = velba / 4;
    int count = elba - lba + 1;
    int before = vlba % 4;
    int after = 3 - (velba % 4);

    struct ata_op_s op;
    op.driveid = driveid;
    op.lba = lba;
    op.count = count;
    op.far_buffer = far_buffer;

    dprintf(16, "cdrom_read_512: id=%d vlba=%d vcount=%d buf=%p lba=%d elba=%d"
            " count=%d before=%d after=%d\n"
            , driveid, vlba, vcount, far_buffer, lba, elba
            , count, before, after);

    int ret = send_cmd_cdrom(&op);
    if (ret)
        return ret;

    return ata_transfer_emu(&op, before, after);
}

// Send a simple atapi command to a drive.
int
ata_cmd_packet(int driveid, u8 *cmdbuf, u8 cmdlen
               , u32 length, void *far_buffer)
{
    int ret = send_atapi_cmd(driveid, cmdbuf, cmdlen, length);
    if (ret)
        return ret;

    return ata_transfer(driveid, 0, 1, length, 0, 0, far_buffer);
}


/****************************************************************
 * ATA detect and init
 ****************************************************************/

static void
report_model(int driveid, u8 *buffer)
{
    u8 model[41];

    // Read model name
    int i;
    for (i=0; i<40; i+=2) {
        model[i] = buffer[i+54+1];
        model[i+1] = buffer[i+54];
    }

    // Reformat
    model[40] = 0x00;
    for (i=39; i>0; i--) {
        if (model[i] != 0x20)
            break;
        model[i] = 0x00;
    }

    u8 channel = driveid / 2;
    u8 slave = driveid % 2;
    // XXX - model on stack not %cs
    printf("ata%d %s: %s", channel, slave ? " slave" : "master", model);
}

static u8
get_ata_version(u8 *buffer)
{
    u16 ataversion = *(u16*)&buffer[160];
    u8 version;
    for (version=15; version>0; version--)
        if (ataversion & (1<<version))
            break;
    return version;
}

static void
init_drive_atapi(int driveid)
{
    SET_EBDA(ata.devices[driveid].type, ATA_TYPE_ATAPI);

    // Temporary values to do the transfer
    SET_EBDA(ata.devices[driveid].device,ATA_DEVICE_CDROM);
    SET_EBDA(ata.devices[driveid].mode, ATA_MODE_PIO16);

    // Now we send a IDENTIFY command to ATAPI device
    u8 buffer[0x0200];
    memset(buffer, 0, sizeof(buffer));
    u16 ret = ata_cmd_data(driveid, ATA_CMD_IDENTIFY_DEVICE_PACKET
                           , 1, 1
                           , MAKE_FARPTR(GET_SEG(SS), (u32)buffer));
    if (ret != 0)
        BX_PANIC("ata-detect: Failed to detect ATAPI device\n");

    u8 type      = buffer[1] & 0x1f;
    u8 removable = (buffer[0] & 0x80) ? 1 : 0;
    u8 mode      = buffer[96] ? ATA_MODE_PIO32 : ATA_MODE_PIO16;
    u16 blksize  = CDROM_SECTOR_SIZE;

    SET_EBDA(ata.devices[driveid].device, type);
    SET_EBDA(ata.devices[driveid].removable, removable);
    SET_EBDA(ata.devices[driveid].mode, mode);
    SET_EBDA(ata.devices[driveid].blksize, blksize);

    // fill cdidmap
    u8 cdcount = GET_EBDA(ata.cdcount);
    SET_EBDA(ata.idmap[1][cdcount], driveid);
    SET_EBDA(ata.cdcount, ++cdcount);

    report_model(driveid, buffer);
    u8 version = get_ata_version(buffer);
    if (GET_EBDA(ata.devices[driveid].device)==ATA_DEVICE_CDROM)
        printf(" ATAPI-%d CD-Rom/DVD-Rom\n", version);
    else
        printf(" ATAPI-%d Device\n", version);
}

static void
fill_fdpt(int driveid)
{
    if (driveid > 1)
        return;

    u16 nlc   = GET_EBDA(ata.devices[driveid].lchs.cylinders);
    u16 nlh   = GET_EBDA(ata.devices[driveid].lchs.heads);
    u16 nlspt = GET_EBDA(ata.devices[driveid].lchs.spt);

    u16 npc   = GET_EBDA(ata.devices[driveid].pchs.cylinders);
    u16 nph   = GET_EBDA(ata.devices[driveid].pchs.heads);
    u16 npspt = GET_EBDA(ata.devices[driveid].pchs.spt);

    SET_EBDA(fdpt[driveid].precompensation, 0xffff);
    SET_EBDA(fdpt[driveid].drive_control_byte, 0xc0 | ((nph > 8) << 3));
    SET_EBDA(fdpt[driveid].landing_zone, npc);
    SET_EBDA(fdpt[driveid].cylinders, nlc);
    SET_EBDA(fdpt[driveid].heads, nlh);
    SET_EBDA(fdpt[driveid].sectors, nlspt);

    if (nlc == npc && nlh == nph && nlspt == npspt)
        // no logical CHS mapping used, just physical CHS
        // use Standard Fixed Disk Parameter Table (FDPT)
        return;

    // complies with Phoenix style Translated Fixed Disk Parameter
    // Table (FDPT)
    SET_EBDA(fdpt[driveid].phys_cylinders, npc);
    SET_EBDA(fdpt[driveid].phys_heads, nph);
    SET_EBDA(fdpt[driveid].phys_sectors, npspt);
    SET_EBDA(fdpt[driveid].a0h_signature, 0xa0);

    // Checksum structure.
    u8 *p = MAKE_FARPTR(SEG_EBDA, offsetof(struct extended_bios_data_area_s
                                           , fdpt[driveid]));
    u8 sum = checksum(p, FIELD_SIZEOF(struct extended_bios_data_area_s
                                      , fdpt[driveid]) - 1);
    SET_EBDA(fdpt[driveid].checksum, -sum);
}

static u8
get_translation(int driveid)
{
    if (! CONFIG_COREBOOT) {
        // Emulators pass in the translation info via nvram.
        u8 channel = driveid / 2;
        u8 translation = inb_cmos(CMOS_BIOS_DISKTRANSFLAG + channel/2);
        translation >>= 2 * (driveid % 4);
        translation &= 0x03;
        return translation;
    }

    // On COREBOOT, use a heuristic to determine translation type.
    u16 heads = GET_EBDA(ata.devices[driveid].pchs.heads);
    u16 cylinders = GET_EBDA(ata.devices[driveid].pchs.cylinders);
    u16 spt = GET_EBDA(ata.devices[driveid].pchs.spt);

    if (cylinders <= 1024 && heads <= 16 && spt <= 63)
        return ATA_TRANSLATION_NONE;
    if (cylinders * heads <= 131072)
        return ATA_TRANSLATION_LARGE;
    return ATA_TRANSLATION_LBA;
}

static void
setup_translation(int driveid)
{
    u8 translation = get_translation(driveid);
    SET_EBDA(ata.devices[driveid].translation, translation);

    u8 channel = driveid / 2;
    u8 slave = driveid % 2;
    u16 heads = GET_EBDA(ata.devices[driveid].pchs.heads);
    u16 cylinders = GET_EBDA(ata.devices[driveid].pchs.cylinders);
    u16 spt = GET_EBDA(ata.devices[driveid].pchs.spt);
    u64 sectors = GET_EBDA(ata.devices[driveid].sectors);

    dprintf(1, "ata%d-%d: PCHS=%u/%d/%d translation="
            , channel, slave, cylinders, heads, spt);
    switch (translation) {
    case ATA_TRANSLATION_NONE:
        dprintf(1, "none");
        break;
    case ATA_TRANSLATION_LBA:
        dprintf(1, "lba");
        spt = 63;
        if (sectors > 63*255*1024) {
            heads = 255;
            cylinders = 1024;
            break;
        }
        u32 sect = (u32)sectors / 63;
        heads = sect / 1024;
        if (heads>128)
            heads = 255;
        else if (heads>64)
            heads = 128;
        else if (heads>32)
            heads = 64;
        else if (heads>16)
            heads = 32;
        else
            heads = 16;
        cylinders = sect / heads;
        break;
    case ATA_TRANSLATION_RECHS:
        dprintf(1, "r-echs");
        // Take care not to overflow
        if (heads==16) {
            if (cylinders>61439)
                cylinders=61439;
            heads=15;
            cylinders = (u16)((u32)(cylinders)*16/15);
        }
        // then go through the large bitshift process
    case ATA_TRANSLATION_LARGE:
        if (translation == ATA_TRANSLATION_LARGE)
            dprintf(1, "large");
        while (cylinders > 1024) {
            cylinders >>= 1;
            heads <<= 1;

            // If we max out the head count
            if (heads > 127)
                break;
        }
        break;
    }
    // clip to 1024 cylinders in lchs
    if (cylinders > 1024)
        cylinders = 1024;
    dprintf(1, " LCHS=%d/%d/%d\n", cylinders, heads, spt);

    SET_EBDA(ata.devices[driveid].lchs.heads, heads);
    SET_EBDA(ata.devices[driveid].lchs.cylinders, cylinders);
    SET_EBDA(ata.devices[driveid].lchs.spt, spt);
}

static void
init_drive_ata(int driveid)
{
    SET_EBDA(ata.devices[driveid].type, ATA_TYPE_ATA);

    // Temporary values to do the transfer
    SET_EBDA(ata.devices[driveid].device, ATA_DEVICE_HD);
    SET_EBDA(ata.devices[driveid].mode, ATA_MODE_PIO16);

    // Now we send a IDENTIFY command to ATA device
    u8 buffer[0x0200];
    memset(buffer, 0, sizeof(buffer));
    u16 ret = ata_cmd_data(driveid, ATA_CMD_IDENTIFY_DEVICE
                           , 1, 1
                           , MAKE_FARPTR(GET_SEG(SS), (u32)buffer));
    if (ret)
        BX_PANIC("ata-detect: Failed to detect ATA device\n");

    u8 removable  = (buffer[0] & 0x80) ? 1 : 0;
    u8 mode       = buffer[48*2] ? ATA_MODE_PIO32 : ATA_MODE_PIO16;
    u16 blksize   = IDE_SECTOR_SIZE;

    u16 cylinders = *(u16*)&buffer[1*2]; // word 1
    u16 heads     = *(u16*)&buffer[3*2]; // word 3
    u16 spt       = *(u16*)&buffer[6*2]; // word 6

    u64 sectors;
    if (*(u16*)&buffer[83*2] & (1 << 10)) // word 83 - lba48 support
        sectors = *(u64*)&buffer[100*2]; // word 100-103
    else
        sectors = *(u32*)&buffer[60*2]; // word 60 and word 61

    SET_EBDA(ata.devices[driveid].device, ATA_DEVICE_HD);
    SET_EBDA(ata.devices[driveid].removable, removable);
    SET_EBDA(ata.devices[driveid].mode, mode);
    SET_EBDA(ata.devices[driveid].blksize, blksize);
    SET_EBDA(ata.devices[driveid].pchs.heads, heads);
    SET_EBDA(ata.devices[driveid].pchs.cylinders, cylinders);
    SET_EBDA(ata.devices[driveid].pchs.spt, spt);
    SET_EBDA(ata.devices[driveid].sectors, sectors);

    // Setup disk geometry translation.
    setup_translation(driveid);

    // fill hdidmap
    u8 hdcount = GET_EBDA(ata.hdcount);
    SET_EBDA(ata.idmap[0][hdcount], driveid);
    SET_EBDA(ata.hdcount, ++hdcount);

    // Fill "fdpt" structure.
    fill_fdpt(driveid);

    // Report drive info to user.
    u64 sizeinmb = GET_EBDA(ata.devices[driveid].sectors) >> 11;
    report_model(driveid, buffer);
    u8 version = get_ata_version(buffer);
    if (sizeinmb < (1 << 16))
        printf(" ATA-%d Hard-Disk (%u MiBytes)\n", version, (u32)sizeinmb);
    else
        printf(" ATA-%d Hard-Disk (%u GiBytes)\n", version
               , (u32)(sizeinmb >> 10));
}

static void
init_drive_unknown(int driveid)
{
    SET_EBDA(ata.devices[driveid].type, ATA_TYPE_UNKNOWN);

    u8 channel = driveid / 2;
    u8 slave = driveid % 2;
    printf("ata%d %s: Unknown device\n", channel, slave ? " slave" : "master");
}

static void
ata_detect()
{
    // Device detection
    int driveid;
    for(driveid=0; driveid<CONFIG_MAX_ATA_DEVICES; driveid++) {
        u8 channel = driveid / 2;
        u8 slave = driveid % 2;

        u16 iobase1 = GET_EBDA(ata.channels[channel].iobase1);
        u16 iobase2 = GET_EBDA(ata.channels[channel].iobase2);
        if (!iobase1)
            break;

        // Disable interrupts
        outb(ATA_CB_DC_HD15 | ATA_CB_DC_NIEN, iobase2+ATA_CB_DC);

        // Look for device
        outb(slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0, iobase1+ATA_CB_DH);
        msleep(50);
        outb(0x55, iobase1+ATA_CB_SC);
        outb(0xaa, iobase1+ATA_CB_SN);
        outb(0xaa, iobase1+ATA_CB_SC);
        outb(0x55, iobase1+ATA_CB_SN);
        outb(0x55, iobase1+ATA_CB_SC);
        outb(0xaa, iobase1+ATA_CB_SN);

        // If we found something
        u8 sc = inb(iobase1+ATA_CB_SC);
        u8 sn = inb(iobase1+ATA_CB_SN);
        dprintf(6, "ata_detect(1) drive=%d sc=%x sn=%x\n", driveid, sc, sn);

        if (sc != 0x55 || sn != 0xaa)
            continue;

        // reset the channel
        ata_reset(driveid);

        // check for ATA or ATAPI
        outb(slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0, iobase1+ATA_CB_DH);
        msleep(50);
        sc = inb(iobase1+ATA_CB_SC);
        sn = inb(iobase1+ATA_CB_SN);
        dprintf(6, "ata_detect(2) drive=%d sc=%x sn=%x\n", driveid, sc, sn);
        if (sc!=0x01 || sn!=0x01) {
            init_drive_unknown(driveid);
            continue;
        }
        u8 cl = inb(iobase1+ATA_CB_CL);
        u8 ch = inb(iobase1+ATA_CB_CH);
        u8 st = inb(iobase1+ATA_CB_STAT);
        dprintf(6, "ata_detect(3) drive=%d sc=%x sn=%x cl=%x ch=%x st=%x\n"
                , driveid, sc, sn, cl, ch, st);

        if (cl==0x14 && ch==0xeb)
            init_drive_atapi(driveid);
        else if (cl==0x00 && ch==0x00 && st!=0x00)
            init_drive_ata(driveid);
        else if (cl==0xff && ch==0xff)
            // None
            continue;
        else
            init_drive_unknown(driveid);
    }

    printf("\n");
}

static void
ata_init()
{
    // hdidmap and cdidmap init.
    u8 device;
    for (device=0; device < CONFIG_MAX_ATA_DEVICES; device++) {
        SET_EBDA(ata.idmap[0][device], CONFIG_MAX_ATA_DEVICES);
        SET_EBDA(ata.idmap[1][device], CONFIG_MAX_ATA_DEVICES);
    }

    // Scan PCI bus for ATA adapters
    int count=0, bdf=-1;
    u16 classid = PCI_CLASS_STORAGE_OTHER; // SATA first
    while (count<CONFIG_MAX_ATA_INTERFACES-1) {
        bdf = pci_find_class(classid, bdf+1);
        if (bdf < 0) {
            if (classid == PCI_CLASS_STORAGE_IDE)
                // Done
                break;
            classid = PCI_CLASS_STORAGE_IDE; // PATA controllers
            bdf = -1;
            continue;
        }

        u8 irq = pci_config_readb(bdf, PCI_INTERRUPT_LINE);
        SET_EBDA(ata.channels[count].irq, irq);
        SET_EBDA(ata.channels[count].pci_bdf, bdf);

        u8 prog_if = pci_config_readb(bdf, PCI_CLASS_PROG);
        u32 port1, port2;

        if (classid != PCI_CLASS_STORAGE_IDE || prog_if & 1) {
            port1 = pci_config_readl(bdf, PCI_BASE_ADDRESS_0) & ~3;
            port2 = pci_config_readl(bdf, PCI_BASE_ADDRESS_1) & ~3;
        } else {
            port1 = 0x1f0;
            port2 = 0x3f0;
        }
        SET_EBDA(ata.channels[count].iobase1, port1);
        SET_EBDA(ata.channels[count].iobase2, port2);
        dprintf(1, "ATA controller %d at %x/%x\n", count, port1, port2);
        count++;

        if (classid != PCI_CLASS_STORAGE_IDE || prog_if & 4) {
            port1 = pci_config_readl(bdf, PCI_BASE_ADDRESS_2) & ~3;
            port2 = pci_config_readl(bdf, PCI_BASE_ADDRESS_3) & ~3;
        } else {
            port1 = 0x170;
            port2 = 0x370;
        }
        dprintf(1, "ATA controller %d at %x/%x\n", count, port1, port2);
        SET_EBDA(ata.channels[count].iobase1, port1);
        SET_EBDA(ata.channels[count].iobase2, port2);
        count++;
    }
}

void
hard_drive_setup()
{
    if (!CONFIG_ATA)
        return;

    dprintf(3, "init hard drives\n");
    ata_init();
    ata_detect();

    // Store the device count
    SET_BDA(disk_count, GET_EBDA(ata.hdcount));

    SET_BDA(disk_control_byte, 0xc0);

    // Enable IRQ14 (handle_76)
    unmask_pic2(PIC2_IRQ14);
}
