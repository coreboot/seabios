// 32bit code to Power On Self Test (POST) a machine.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "ioport.h" // PORT_*
#include "../out/rom16.offset.auto.h" // OFFSET_*
#include "config.h" // CONFIG_*
#include "cmos.h" // CMOS_*
#include "util.h" // memset
#include "biosvar.h" // struct bios_data_area_s

#define bda ((struct bios_data_area_s *)0)
#define ebda ((struct extended_bios_data_area_s *)(EBDA_SEG<<4))

static void
init_bda()
{
    memset(bda, 0, sizeof(*bda));

    int i;
    for (i=0; i<256; i++) {
        bda->ivecs[i].seg = 0xf000;
        bda->ivecs[i].offset = OFFSET_dummy_iret_handler;
    }

    bda->mem_size_kb = BASE_MEM_IN_K;
}

static void
init_handlers()
{
    // set vector 0x79 to zero
    // this is used by 'gardian angel' protection system
    bda->ivecs[0x79].seg = 0;
    bda->ivecs[0x79].offset = 0;

    bda->ivecs[0x40].offset = OFFSET_entry_40;
    bda->ivecs[0x0e].offset = OFFSET_entry_0e;
    bda->ivecs[0x13].offset = OFFSET_entry_13;
    bda->ivecs[0x76].offset = OFFSET_entry_76;
    bda->ivecs[0x17].offset = OFFSET_entry_17;
    bda->ivecs[0x18].offset = OFFSET_entry_18;
    bda->ivecs[0x19].offset = OFFSET_entry_19;
    bda->ivecs[0x1c].offset = OFFSET_entry_1c;
    bda->ivecs[0x12].offset = OFFSET_entry_12;
    bda->ivecs[0x11].offset = OFFSET_entry_11;
    bda->ivecs[0x15].offset = OFFSET_entry_15;
    bda->ivecs[0x08].offset = OFFSET_entry_08;
    bda->ivecs[0x09].offset = OFFSET_entry_09;
    bda->ivecs[0x16].offset = OFFSET_entry_16;
    bda->ivecs[0x14].offset = OFFSET_entry_14;
    bda->ivecs[0x1a].offset = OFFSET_entry_1a;
    bda->ivecs[0x70].offset = OFFSET_entry_70;
    bda->ivecs[0x74].offset = OFFSET_entry_74;
    bda->ivecs[0x75].offset = OFFSET_entry_75;
    bda->ivecs[0x10].offset = OFFSET_entry_10;
}

static void
init_ebda()
{
    ebda->size = EBDA_SIZE;
    bda->ebda_seg = EBDA_SEG;
    bda->ivecs[0x41].seg = EBDA_SEG;
    bda->ivecs[0x41].offset = 0x3d; // XXX
    bda->ivecs[0x46].seg = EBDA_SEG;
    bda->ivecs[0x46].offset = 0x4d; // XXX
}

static void
pit_setup()
{
    // timer0: binary count, 16bit count, mode 2
    outb(0x34, PORT_PIT_MODE);
    // maximum count of 0000H = 18.2Hz
    outb(0x0, PORT_PIT_COUNTER0);
    outb(0x0, PORT_PIT_COUNTER0);
}

static void
kbd_init()
{
}

static void
kbd_setup()
{
    bda->kbd_mode = 0x10;
    bda->kbd_buf_head = bda->kbd_buf_tail = offsetof(struct bios_data_area_s, kbd_buf);
    bda->kbd_buf_start_offset = offsetof(struct bios_data_area_s, kbd_buf);
    bda->kbd_buf_end_offset = offsetof(struct bios_data_area_s, kbd_buf[sizeof(bda->kbd_buf)]);
    kbd_init();

    // XXX
    u16 eqb = bda->equipment_list_flags;
    eqb = (eqb & 0xff00) | inb_cmos(CMOS_EQUIPMENT_INFO);
    bda->equipment_list_flags = eqb;
}

static void
lpt_setup()
{
    // XXX
}

static void
serial_setup()
{
    // XXX
}

static u32
bcd2bin(u8 val)
{
    return (val & 0xf) + ((val >> 4) * 10);
}

static void
timer_setup()
{
    u32 seconds = bcd2bin(inb_cmos(CMOS_RTC_SECONDS));
    u32 ticks = (seconds * 18206507) / 1000000;
    u32 minutes = bcd2bin(inb_cmos(CMOS_RTC_MINUTES));
    ticks += (minutes * 10923904) / 10000;
    u32 hours = bcd2bin(inb_cmos(CMOS_RTC_HOURS));
    ticks += (hours * 65543427) / 1000;
    bda->timer_counter = ticks;
    bda->timer_rollover = 0;
}

static void
pic_setup()
{
    outb(0x11, PORT_PIC1);
    outb(0x11, PORT_PIC2_DATA);
    outb(0x08, PORT_PIC1_DATA);
    outb(0x70, PORT_PIC2_DATA);
    outb(0x04, PORT_PIC1_DATA);
    outb(0x02, PORT_PIC2_DATA);
    outb(0x01, PORT_PIC1_DATA);
    outb(0x01, PORT_PIC2_DATA);
    outb(0xb8, PORT_PIC1_DATA);
    if (CONFIG_PS2_MOUSE)
        outb(0x8f, PORT_PIC2_DATA);
    else
        outb(0x9f, PORT_PIC2_DATA);
}

static void
floppy_drive_post()
{
    u8 type = inb_cmos(CMOS_FLOPPY_DRIVE_TYPE);
    u8 out = 0;
    if (type & 0xf0)
        out |= 0x07;
    if (type & 0x0f)
        out |= 0x70;
    bda->floppy_harddisk_info = out;
    outb(0x02, PORT_DMA1_MASK_REG);

    bda->ivecs[0x1E].offset = OFFSET_diskette_param_table2;
}

static void
cdemu_init()
{
    //ebda->cdemu.active = 0;
}

static void
ata_init()
{
}

static void
ata_detect()
{
}

static void
hard_drive_post()
{
}

static void
init_boot_vectors()
{
}

static void __attribute__((noinline))
call16(u16 seg, u16 offset)
{
    u32 segoff = (seg << 16) | offset;
    asm volatile(
        "pushal\n"  // Save registers
        "ljmp $0x20, %0\n" // Jump to 16bit transition code
        ".globl call16_resume\n"
        "call16_resume:\n"  // point of return
        "popal\n"   // restore registers
        : : "Z" (OFFSET_call16), "b" (segoff));
}

static int
checksum(u8 *p, u32 len)
{
    u32 i;
    u8 sum = 0;
    for (i=0; i<len; i++)
        sum += p[i];
    return sum;
}

#define PTR_TO_SEG(p) ((((u32)(p)) >> 4) & 0xf000)
#define PTR_TO_OFFSET(p) (((u32)(p)) & 0xffff)

static void
rom_scan()
{
    u8 *p = (u8*)0xc0000;
    for (; p <= (u8*)0xe0000; p += 2048) {
        u8 *rom = p;
        if (*(u16*)rom != 0xaa55)
            continue;
        u32 len = rom[2] * 512;
        if (checksum(rom, len) != 0)
            continue;
        p = (u8*)(((u32)p + len) / 2048 * 2048);
        call16(PTR_TO_SEG(rom), PTR_TO_OFFSET(rom + 3));

        // Look at the ROM's PnP Expansion header.  Properly, we're supposed
        // to init all the ROMs and then go back and build an IPL table of
        // all the bootable devices, but we can get away with one pass.
        if (rom[0x1a] != '$' || rom[0x1b] != 'P'
            || rom[0x1c] != 'n' || rom[0x1d] != 'P')
            continue;
        // 0x1A is also the offset into the expansion header of...
        // the Bootstrap Entry Vector, or zero if there is none.
        u16 entry = *(u16*)&rom[0x1a+0x1a];
        if (!entry)
            continue;
        // Found a device that thinks it can boot the system.  Record
        // its BEV and product name string.

        // XXX
    }
}

static void
status_restart(u8 status)
{
#if 0
    if (status == 0x05)
        eoi_jmp_post();
#endif

    BX_PANIC("Unimplemented shutdown status: %02x\n",(Bit8u)status);
}

static void
post()
{
    // first reset the DMA controllers
    outb(0, PORT_DMA1_MASTER_CLEAR);
    outb(0, PORT_DMA2_MASTER_CLEAR);

    // then initialize the DMA controllers
    outb(0xc0, PORT_DMA2_MODE_REG);
    outb(0x00, PORT_DMA2_MASK_REG);

    // Get and then clear CMOS shutdown status.
    u8 status = inb_cmos(CMOS_RESET_CODE);
    outb_cmos(0, CMOS_RESET_CODE);

    if (status != 0x00 && status != 0x09 && status < 0x0d)
        status_restart(status);

    BX_INFO("Start bios");

    init_bda();
    init_handlers();
    init_ebda();

    pit_setup();
    kbd_setup();
    lpt_setup();
    serial_setup();
    timer_setup();
    pic_setup();
    //pci_setup();
    init_boot_vectors();
    rom_scan();

    printf("BIOS - begin\n\n");

    floppy_drive_post();
    hard_drive_post();
    if (CONFIG_ATA) {
        ata_init();
        ata_detect();
    }
    cdemu_init();
    call16(0xf000, OFFSET_begin_boot);
}

void VISIBLE
_start()
{
    post();
}
