// 16bit code to access floppy drives.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "types.h" // u8
#include "disk.h" // DISK_RET_SUCCESS
#include "config.h" // CONFIG_FLOPPY_SUPPORT
#include "biosvar.h" // SET_BDA
#include "util.h" // irq_disable
#include "cmos.h" // inb_cmos
#include "pic.h" // eoi_pic1
#include "bregs.h" // struct bregs

#define BX_FLOPPY_ON_CNT 37   /* 2 seconds */

// New diskette parameter table adding 3 parameters from IBM
// Since no provisions are made for multiple drive types, most
// values in this table are ignored.  I set parameters for 1.44M
// floppy here
struct floppy_ext_dbt_s diskette_param_table2 VAR16_32 = {
    .dbt = {
        .specify1       = 0xAF,
        .specify2       = 0x02, // head load time 0000001, DMA used
        .shutoff_ticks  = 0x25,
        .bps_code       = 0x02,
        .sectors        = 18,
        .interblock_len = 0x1B,
        .data_len       = 0xFF,
        .gap_len        = 0x6C,
        .fill_byte      = 0xF6,
        .settle_time    = 0x0F,
        .startup_time   = 0x08,
    },
    .max_track      = 79,   // maximum track
    .data_rate      = 0,    // data transfer rate
    .drive_type     = 4,    // drive type in cmos
};

// Since no provisions are made for multiple drive types, most
// values in this table are ignored.  I set parameters for 1.44M
// floppy here
struct floppy_dbt_s diskette_param_table VAR16FIXED(0xefc7) = {
    .specify1       = 0xAF,
    .specify2       = 0x02, // head load time 0000001, DMA used
    .shutoff_ticks  = 0x25,
    .bps_code       = 0x02,
    .sectors        = 18,
    .interblock_len = 0x1B,
    .data_len       = 0xFF,
    .gap_len        = 0x6C,
    .fill_byte      = 0xF6,
    .settle_time    = 0x0F,
    .startup_time   = 0x08,
};

void
floppy_drive_setup()
{
    if (! CONFIG_FLOPPY_SUPPORT)
        return;
    dprintf(3, "init floppy drives\n");
    if (CONFIG_COREBOOT)
        // XXX - disable floppies on coreboot for now.
        outb_cmos(0, CMOS_FLOPPY_DRIVE_TYPE);
    u8 type = inb_cmos(CMOS_FLOPPY_DRIVE_TYPE);
    u8 out = 0;
    u8 num_floppies = 0;

    if (type & 0xf0) {
        out |= 0x07;
        num_floppies++;
    }
    if (type & 0x0f) {
        out |= 0x70;
        num_floppies++;
    }
    SET_BDA(floppy_harddisk_info, out);

    // Update equipment word bits for floppy
    if (num_floppies == 1)
        // 1 drive, ready for boot
        SETBITS_BDA(equipment_list_flags, 0x01);
    else if (num_floppies == 2)
        // 2 drives, ready for boot
        SETBITS_BDA(equipment_list_flags, 0x41);

    outb(0x02, PORT_DMA1_MASK_REG);

    enable_hwirq(6, entry_0e);
}

static inline void
set_diskette_current_cyl(u8 drive, u8 cyl)
{
    if (drive)
        SET_BDA(floppy_track1, cyl);
    else
        SET_BDA(floppy_track0, cyl);
}

static u16
get_drive_type(u8 drive)
{
    // check CMOS to see if drive exists
    u8 drive_type = inb_cmos(CMOS_FLOPPY_DRIVE_TYPE);
    if (drive == 0)
        drive_type >>= 4;
    else
        drive_type &= 0x0f;
    return drive_type;
}

static u16
floppy_media_known(u8 drive)
{
    if (!(GET_BDA(floppy_recalibration_status) & (1<<drive)))
        return 0;
    u8 v = GET_BDA(floppy_media_state[drive]);
    if (!(v & FMS_MEDIA_DRIVE_ESTABLISHED))
        return 0;
    return 1;
}

static void
floppy_reset_controller()
{
    // Reset controller
    u8 val8 = inb(PORT_FD_DOR);
    outb(val8 & ~0x04, PORT_FD_DOR);
    outb(val8 | 0x04, PORT_FD_DOR);

    // Wait for controller to come out of reset
    while ((inb(PORT_FD_STATUS) & 0xc0) != 0x80)
        ;
}

static void
floppy_prepare_controller(u8 drive)
{
    CLEARBITS_BDA(floppy_recalibration_status, FRS_TIMEOUT);

    // turn on motor of selected drive, DMA & int enabled, normal operation
    u8 prev_reset = inb(PORT_FD_DOR) & 0x04;
    u8 dor = 0x10;
    if (drive)
        dor = 0x20;
    dor |= 0x0c;
    dor |= drive;
    outb(dor, PORT_FD_DOR);

    // reset the disk motor timeout value of INT 08
    SET_BDA(floppy_motor_counter, BX_FLOPPY_ON_CNT);

    // wait for drive readiness
    while ((inb(PORT_FD_STATUS) & 0xc0) != 0x80)
        ;

    if (prev_reset == 0) {
        irq_enable();
        u8 v;
        for (;;) {
            v = GET_BDA(floppy_recalibration_status);
            if (v & FRS_TIMEOUT)
                break;
            cpu_relax();
        }
        irq_disable();

        v &= ~FRS_TIMEOUT;
        SET_BDA(floppy_recalibration_status, v);
    }
}

static int
floppy_pio(u8 *cmd, u8 cmdlen)
{
    floppy_prepare_controller(cmd[1] & 1);

    // send command to controller
    u8 i;
    for (i=0; i<cmdlen; i++)
        outb(cmd[i], PORT_FD_DATA);

    irq_enable();
    u8 v;
    for (;;) {
        if (!GET_BDA(floppy_motor_counter)) {
            irq_disable();
            floppy_reset_controller();
            return -1;
        }
        v = GET_BDA(floppy_recalibration_status);
        if (v & FRS_TIMEOUT)
            break;
        cpu_relax();
    }
    irq_disable();

    v &= ~FRS_TIMEOUT;
    SET_BDA(floppy_recalibration_status, v);

    return 0;
}

#define floppy_ret(regs, code)                                  \
    __floppy_ret((regs), (code) | (__LINE__ << 8), __func__)

void
__floppy_ret(struct bregs *regs, u32 linecode, const char *fname)
{
    u8 code = linecode;
    SET_BDA(floppy_last_status, code);
    if (code)
        __set_code_fail(regs, linecode, fname);
    else
        set_code_success(regs);
}

static int
floppy_cmd(struct bregs *regs, u16 count, u8 *cmd, u8 cmdlen)
{
    // es:bx = pointer to where to place information from diskette
    u32 addr = (u32)MAKE_FLATPTR(regs->es, regs->bx);

    // check for 64K boundary overrun
    u32 last_addr = addr + count;
    if ((addr >> 16) != (last_addr >> 16)) {
        floppy_ret(regs, DISK_RET_EBOUNDARY);
        return -1;
    }

    u8 mode_register = 0x4a; // single mode, increment, autoinit disable,
    if (cmd[0] == 0xe6)
        // read
        mode_register = 0x46;

    //DEBUGF("floppy dma c2\n");
    outb(0x06, PORT_DMA1_MASK_REG);
    outb(0x00, PORT_DMA1_CLEAR_FF_REG); // clear flip-flop
    outb(addr, PORT_DMA_ADDR_2);
    outb(addr>>8, PORT_DMA_ADDR_2);
    outb(0x00, PORT_DMA1_CLEAR_FF_REG); // clear flip-flop
    outb(count, PORT_DMA_CNT_2);
    outb(count>>8, PORT_DMA_CNT_2);

    // port 0b: DMA-1 Mode Register
    // transfer type=write, channel 2
    outb(mode_register, PORT_DMA1_MODE_REG);

    // port 81: DMA-1 Page Register, channel 2
    outb(addr>>16, PORT_DMA_PAGE_2);

    outb(0x02, PORT_DMA1_MASK_REG); // unmask channel 2

    int ret = floppy_pio(cmd, cmdlen);
    if (ret) {
        floppy_ret(regs, DISK_RET_ETIMEOUT);
        return -1;
    }

    // check port 3f4 for accessibility to status bytes
    if ((inb(PORT_FD_STATUS) & 0xc0) != 0xc0)
        BX_PANIC("int13_diskette: ctrl not ready\n");

    // read 7 return status bytes from controller
    u8 i;
    for (i=0; i<7; i++) {
        u8 v = inb(PORT_FD_DATA);
        cmd[i] = v;
        SET_BDA(floppy_return_status[i], v);
    }

    return 0;
}

static void
floppy_drive_recal(u8 drive)
{
    // send Recalibrate command (2 bytes) to controller
    u8 data[12];
    data[0] = 0x07;  // 07: Recalibrate
    data[1] = drive; // 0=drive0, 1=drive1
    floppy_pio(data, 2);

    SETBITS_BDA(floppy_recalibration_status, 1<<drive);
    set_diskette_current_cyl(drive, 0);
}

static u16
floppy_media_sense(u8 drive)
{
    u16 rv;
    u8 config_data, media_state;

    floppy_drive_recal(drive);

    // for now cheat and get drive type from CMOS,
    // assume media is same as drive type

    // ** config_data **
    // Bitfields for diskette media control:
    // Bit(s)  Description (Table M0028)
    //  7-6  last data rate set by controller
    //        00=500kbps, 01=300kbps, 10=250kbps, 11=1Mbps
    //  5-4  last diskette drive step rate selected
    //        00=0Ch, 01=0Dh, 10=0Eh, 11=0Ah
    //  3-2  {data rate at start of operation}
    //  1-0  reserved

    // ** media_state **
    // Bitfields for diskette drive media state:
    // Bit(s)  Description (Table M0030)
    //  7-6  data rate
    //    00=500kbps, 01=300kbps, 10=250kbps, 11=1Mbps
    //  5  double stepping required (e.g. 360kB in 1.2MB)
    //  4  media type established
    //  3  drive capable of supporting 4MB media
    //  2-0  on exit from BIOS, contains
    //    000 trying 360kB in 360kB
    //    001 trying 360kB in 1.2MB
    //    010 trying 1.2MB in 1.2MB
    //    011 360kB in 360kB established
    //    100 360kB in 1.2MB established
    //    101 1.2MB in 1.2MB established
    //    110 reserved
    //    111 all other formats/drives

    switch (get_drive_type(drive)) {
    case 1:
        // 360K 5.25" drive
        config_data = 0x00; // 0000 0000
        media_state = 0x25; // 0010 0101
        rv = 1;
        break;
    case 2:
        // 1.2 MB 5.25" drive
        config_data = 0x00; // 0000 0000
        media_state = 0x25; // 0010 0101   // need double stepping??? (bit 5)
        rv = 1;
        break;
    case 3:
        // 720K 3.5" drive
        config_data = 0x00; // 0000 0000 ???
        media_state = 0x17; // 0001 0111
        rv = 1;
        break;
    case 4:
        // 1.44 MB 3.5" drive
        config_data = 0x00; // 0000 0000
        media_state = 0x17; // 0001 0111
        rv = 1;
        break;
    case 5:
        // 2.88 MB 3.5" drive
        config_data = 0xCC; // 1100 1100
        media_state = 0xD7; // 1101 0111
        rv = 1;
        break;
    //
    // Extended floppy size uses special cmos setting
    case 6:
        // 160k 5.25" drive
        config_data = 0x00; // 0000 0000
        media_state = 0x27; // 0010 0111
        rv = 1;
        break;
    case 7:
        // 180k 5.25" drive
        config_data = 0x00; // 0000 0000
        media_state = 0x27; // 0010 0111
        rv = 1;
        break;
    case 8:
        // 320k 5.25" drive
        config_data = 0x00; // 0000 0000
        media_state = 0x27; // 0010 0111
        rv = 1;
        break;
    default:
        // not recognized
        config_data = 0x00; // 0000 0000
        media_state = 0x00; // 0000 0000
        rv = 0;
    }

    SET_BDA(floppy_last_data_rate, config_data);
    SET_BDA(floppy_media_state[drive], media_state);
    return rv;
}

static int
check_drive(struct bregs *regs, u8 drive)
{
    // see if drive exists
    if (drive > 1 || !get_drive_type(drive)) {
        // XXX - return type doesn't match
        floppy_ret(regs, DISK_RET_ETIMEOUT);
        return -1;
    }

    // see if media in drive, and type is known
    if (floppy_media_known(drive) == 0 && floppy_media_sense(drive) == 0) {
        floppy_ret(regs, DISK_RET_EMEDIA);
        return -1;
    }
    return 0;
}

// diskette controller reset
static void
floppy_1300(struct bregs *regs, u8 drive)
{
    if (drive > 1) {
        floppy_ret(regs, DISK_RET_EPARAM);
        return;
    }
    if (!get_drive_type(drive)) {
        floppy_ret(regs, DISK_RET_ETIMEOUT);
        return;
    }
    set_diskette_current_cyl(drive, 0); // current cylinder
    floppy_ret(regs, DISK_RET_SUCCESS);
}

// Read Diskette Status
static void
floppy_1301(struct bregs *regs, u8 drive)
{
    u8 v = GET_BDA(floppy_last_status);
    regs->ah = v;
    set_cf(regs, v);
}

// Read Diskette Sectors
static void
floppy_1302(struct bregs *regs, u8 drive)
{
    if (check_drive(regs, drive))
        goto fail;

    u8 num_sectors = regs->al;
    u8 track       = regs->ch;
    u8 sector      = regs->cl;
    u8 head        = regs->dh;

    if (head > 1 || sector == 0 || num_sectors == 0
        || track > 79 || num_sectors > 72) {
        floppy_ret(regs, DISK_RET_EPARAM);
        goto fail;
    }

    // send read-normal-data command (9 bytes) to controller
    u8 data[12];
    data[0] = 0xe6; // e6: read normal data
    data[1] = (head << 2) | drive; // HD DR1 DR2
    data[2] = track;
    data[3] = head;
    data[4] = sector;
    data[5] = 2; // 512 byte sector size
    data[6] = sector + num_sectors - 1; // last sector to read on track
    data[7] = 0; // Gap length
    data[8] = 0xff; // Gap length

    int ret = floppy_cmd(regs, (num_sectors * 512) - 1, data, 9);
    if (ret)
        goto fail;

    if (data[0] & 0xc0) {
        floppy_ret(regs, DISK_RET_ECONTROLLER);
        goto fail;
    }

    // ??? should track be new val from return_status[3] ?
    set_diskette_current_cyl(drive, track);
    // AL = number of sectors read (same value as passed)
    floppy_ret(regs, DISK_RET_SUCCESS);
    return;
fail:
    regs->al = 0; // no sectors read
}

// Write Diskette Sectors
static void
floppy_1303(struct bregs *regs, u8 drive)
{
    if (check_drive(regs, drive))
        goto fail;

    u8 num_sectors = regs->al;
    u8 track       = regs->ch;
    u8 sector      = regs->cl;
    u8 head        = regs->dh;

    if (head > 1 || sector == 0 || num_sectors == 0
        || track > 79 || num_sectors > 72) {
        floppy_ret(regs, DISK_RET_EPARAM);
        goto fail;
    }

    // send write-normal-data command (9 bytes) to controller
    u8 data[12];
    data[0] = 0xc5; // c5: write normal data
    data[1] = (head << 2) | drive; // HD DR1 DR2
    data[2] = track;
    data[3] = head;
    data[4] = sector;
    data[5] = 2; // 512 byte sector size
    data[6] = sector + num_sectors - 1; // last sector to write on track
    data[7] = 0; // Gap length
    data[8] = 0xff; // Gap length

    int ret = floppy_cmd(regs, (num_sectors * 512) - 1, data, 9);
    if (ret)
        goto fail;

    if (data[0] & 0xc0) {
        if (data[1] & 0x02) {
            floppy_ret(regs, DISK_RET_EWRITEPROTECT);
            goto fail;
        }
        BX_PANIC("int13_diskette_function: read error\n");
    }

    // ??? should track be new val from return_status[3] ?
    set_diskette_current_cyl(drive, track);
    // AL = number of sectors read (same value as passed)
    floppy_ret(regs, DISK_RET_SUCCESS);
    return;
fail:
    regs->al = 0; // no sectors read
}

// Verify Diskette Sectors
static void
floppy_1304(struct bregs *regs, u8 drive)
{
    if (check_drive(regs, drive))
        goto fail;

    u8 num_sectors = regs->al;
    u8 track       = regs->ch;
    u8 sector      = regs->cl;
    u8 head        = regs->dh;

    if (head > 1 || sector == 0 || num_sectors == 0
        || track > 79 || num_sectors > 72) {
        floppy_ret(regs, DISK_RET_EPARAM);
        goto fail;
    }

    // ??? should track be new val from return_status[3] ?
    set_diskette_current_cyl(drive, track);
    // AL = number of sectors verified (same value as passed)
    floppy_ret(regs, DISK_RET_SUCCESS);
    return;
fail:
    regs->al = 0; // no sectors read
}

// format diskette track
static void
floppy_1305(struct bregs *regs, u8 drive)
{
    dprintf(3, "floppy f05\n");

    if (check_drive(regs, drive))
        return;

    u8 num_sectors = regs->al;
    u8 head        = regs->dh;

    if (head > 1 || num_sectors == 0 || num_sectors > 18) {
        floppy_ret(regs, DISK_RET_EPARAM);
        return;
    }

    // send format-track command (6 bytes) to controller
    u8 data[12];
    data[0] = 0x4d; // 4d: format track
    data[1] = (head << 2) | drive; // HD DR1 DR2
    data[2] = 2; // 512 byte sector size
    data[3] = num_sectors; // number of sectors per track
    data[4] = 0; // Gap length
    data[5] = 0xf6; // Fill byte

    int ret = floppy_cmd(regs, (num_sectors * 4) - 1, data, 6);
    if (ret)
        return;

    if (data[0] & 0xc0) {
        if (data[1] & 0x02) {
            floppy_ret(regs, DISK_RET_EWRITEPROTECT);
            return;
        }
        BX_PANIC("int13_diskette_function: read error\n");
    }

    set_diskette_current_cyl(drive, 0);
    floppy_ret(regs, 0);
}

// read diskette drive parameters
static void
floppy_1308(struct bregs *regs, u8 drive)
{
    dprintf(3, "floppy f08\n");

    u8 drive_type = inb_cmos(CMOS_FLOPPY_DRIVE_TYPE);
    u8 num_floppies = 0;
    if (drive_type & 0xf0)
        num_floppies++;
    if (drive_type & 0x0f)
        num_floppies++;

    if (drive > 1) {
        regs->ax = 0;
        regs->bx = 0;
        regs->cx = 0;
        regs->dx = 0;
        regs->es = 0;
        regs->di = 0;
        regs->dl = num_floppies;
        set_fail(regs);
        return;
    }

    if (drive == 0)
        drive_type >>= 4;
    else
        drive_type &= 0x0f;

    regs->bh = 0;
    regs->bl = drive_type;
    regs->ah = 0;
    regs->al = 0;
    regs->dl = num_floppies;

    switch (drive_type) {
    case 0: // none
        regs->cx = 0;
        regs->dh = 0; // max head #
        break;

    case 1: // 360KB, 5.25"
        regs->cx = 0x2709; // 40 tracks, 9 sectors
        regs->dh = 1; // max head #
        break;

    case 2: // 1.2MB, 5.25"
        regs->cx = 0x4f0f; // 80 tracks, 15 sectors
        regs->dh = 1; // max head #
        break;

    case 3: // 720KB, 3.5"
        regs->cx = 0x4f09; // 80 tracks, 9 sectors
        regs->dh = 1; // max head #
        break;

    case 4: // 1.44MB, 3.5"
        regs->cx = 0x4f12; // 80 tracks, 18 sectors
        regs->dh = 1; // max head #
        break;

    case 5: // 2.88MB, 3.5"
        regs->cx = 0x4f24; // 80 tracks, 36 sectors
        regs->dh = 1; // max head #
        break;

    case 6: // 160k, 5.25"
        regs->cx = 0x2708; // 40 tracks, 8 sectors
        regs->dh = 0; // max head #
        break;

    case 7: // 180k, 5.25"
        regs->cx = 0x2709; // 40 tracks, 9 sectors
        regs->dh = 0; // max head #
        break;

    case 8: // 320k, 5.25"
        regs->cx = 0x2708; // 40 tracks, 8 sectors
        regs->dh = 1; // max head #
        break;

    default: // ?
        BX_PANIC("floppy: int13: bad floppy type\n");
    }

    /* set es & di to point to 11 byte diskette param table in ROM */
    regs->es = SEG_BIOS;
    regs->di = (u32)&diskette_param_table2;
    /* disk status not changed upon success */
    set_success(regs);
}

// read diskette drive type
static void
floppy_1315(struct bregs *regs, u8 drive)
{
    dprintf(6, "floppy f15\n");
    if (drive > 1) {
        set_fail(regs);
        regs->ah = 0; // only 2 drives supported
        // set_diskette_ret_status here ???
        return;
    }
    u8 drive_type = get_drive_type(drive);

    regs->ah = (drive_type != 0);
    set_success(regs);
}

// get diskette change line status
static void
floppy_1316(struct bregs *regs, u8 drive)
{
    if (drive > 1) {
        floppy_ret(regs, DISK_RET_EPARAM);
        return;
    }
    floppy_ret(regs, DISK_RET_ECHANGED);
}

static void
floppy_13XX(struct bregs *regs, u8 drive)
{
    floppy_ret(regs, DISK_RET_EPARAM);
}

void
floppy_13(struct bregs *regs, u8 drive)
{
    if (! CONFIG_FLOPPY_SUPPORT) {
        // Minimal stubs
        switch (regs->ah) {
        case 0x01: floppy_1301(regs, drive); break;
        default:   floppy_13XX(regs, drive); break;
        }
        return;
    }
    switch (regs->ah) {
    case 0x00: floppy_1300(regs, drive); break;
    case 0x01: floppy_1301(regs, drive); break;
    case 0x02: floppy_1302(regs, drive); break;
    case 0x03: floppy_1303(regs, drive); break;
    case 0x04: floppy_1304(regs, drive); break;
    case 0x05: floppy_1305(regs, drive); break;
    case 0x08: floppy_1308(regs, drive); break;
    case 0x15: floppy_1315(regs, drive); break;
    case 0x16: floppy_1316(regs, drive); break;
    default:   floppy_13XX(regs, drive); break;
    }
}

// INT 0Eh Diskette Hardware ISR Entry Point
void VISIBLE16
handle_0e()
{
    debug_isr(DEBUG_ISR_0e);
    if (! CONFIG_FLOPPY_SUPPORT)
        goto done;

    if ((inb(PORT_FD_STATUS) & 0xc0) != 0xc0) {
        outb(0x08, PORT_FD_DATA); // sense interrupt status
        while ((inb(PORT_FD_STATUS) & 0xc0) != 0xc0)
            ;
        do {
            inb(PORT_FD_DATA);
        } while ((inb(PORT_FD_STATUS) & 0xc0) == 0xc0);
    }
    // diskette interrupt has occurred
    SETBITS_BDA(floppy_recalibration_status, FRS_TIMEOUT);

done:
    eoi_pic1();
}

// Called from int08 handler.
void
floppy_tick()
{
    if (! CONFIG_FLOPPY_SUPPORT)
        return;

    // time to turn off drive(s)?
    u8 fcount = GET_BDA(floppy_motor_counter);
    if (fcount) {
        fcount--;
        SET_BDA(floppy_motor_counter, fcount);
        if (fcount == 0)
            // turn motor(s) off
            outb(inb(PORT_FD_DOR) & 0xcf, PORT_FD_DOR);
    }
}
