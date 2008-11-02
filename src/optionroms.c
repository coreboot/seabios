// Option rom scanning code.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "bregs.h" // struct bregs
#include "biosvar.h" // struct ipl_entry_s
#include "util.h" // dprintf

// $PnP string with special alignment in romlayout.S
extern char pnp_string[];

struct rom_header {
    u16 signature;
    u8 size;
    u8 initVector[4];
    u8 reserved[17];
    u16 pcioffset;
    u16 pnpoffset;
} PACKED;

struct pci_data {
    u32 signature;
    u16 vendor;
    u16 device;
    u16 vitaldata;
    u16 dlen;
    u8 drevision;
    u8 class_lo;
    u16 class_hi;
    u16 ilen;
    u16 irevision;
    u8 type;
    u8 indicator;
    u16 reserved;
} PACKED;

struct pnp_data {
    u32 signature;
    u8 revision;
    u8 len;
    u16 nextoffset;
    u8 reserved_08;
    u8 checksum;
    u32 devid;
    u16 manufacturer;
    u16 productname;
    u8 type_lo;
    u16 type_hi;
    u8 dev_flags;
    u16 bcv;
    u16 dv;
    u16 bev;
    u16 reserved_1c;
    u16 staticresource;
} PACKED;

// Execute a given option rom.
static void
callrom(u16 seg, u16 offset)
{
    dprintf(1, "Running option rom at %x:%x\n", seg, offset);

    struct bregs br;
    memset(&br, 0, sizeof(br));
    // XXX - should set br.ax to PCI Bus/DevFn
    br.bx = 0xffff;
    br.dx = 0xffff;
    br.es = SEG_BIOS;
    br.di = (u32)pnp_string - BUILD_BIOS_ADDR;
    br.cs = seg;
    br.ip = offset;
    call16(&br);

    debug_serial_setup();

    if (GET_BDA(ebda_seg) != SEG_EBDA)
        BX_PANIC("Option rom at %x:%x attempted to move ebda from %x to %x\n"
                 , seg, offset, SEG_EBDA, GET_BDA(ebda_seg));
}

#define ebda ((struct extended_bios_data_area_s *)MAKE_FARPTR(SEG_EBDA, 0))

// Find and run any "option roms" found in the given address range.
static void
rom_scan(u32 start, u32 end)
{
    if (! CONFIG_OPTIONROMS)
        return;

    u8 *p = (u8*)start;
    for (; p < (u8*)end; p += 2048) {
        struct rom_header *rom = (struct rom_header *)p;
        if (rom->signature != 0xaa55)
            continue;
        u32 len = rom->size * 512;
        u8 sum = checksum(p, len);
        if (sum != 0) {
            dprintf(1, "Found option rom with bad checksum:"
                    " loc=%p len=%d sum=%x\n"
                    , rom, len, sum);
            continue;
        }
        p = (u8*)(((u32)p + len) / 2048 * 2048);
        callrom(FARPTR_TO_SEG(rom), FARPTR_TO_OFFSET(rom->initVector));

        // Look at the ROM's PnP Expansion header.  Properly, we're supposed
        // to init all the ROMs and then go back and build an IPL table of
        // all the bootable devices, but we can get away with one pass.
        struct pnp_data *pnp = (struct pnp_data *)((u8*)rom + rom->pnpoffset);
        if (pnp->signature != *(u32*)pnp_string)
            continue;
        u16 entry = pnp->bev;
        if (!entry)
            continue;
        // Found a device that thinks it can boot the system.  Record
        // its BEV and product name string.

        if (! CONFIG_BOOT)
            continue;

        if (ebda->ipl.count >= ARRAY_SIZE(ebda->ipl.table))
            continue;

        struct ipl_entry_s *ip = &ebda->ipl.table[ebda->ipl.count];
        ip->type = IPL_TYPE_BEV;
        ip->vector = (FARPTR_TO_SEG(rom) << 16) | entry;

        u16 desc = pnp->productname;
        if (desc)
            ip->description = (u32)MAKE_FARPTR(FARPTR_TO_SEG(rom), desc);

        ebda->ipl.count++;
    }
}

// Call into vga code to turn on console.
void
vga_setup()
{
    dprintf(1, "Scan for VGA option rom\n");
    rom_scan(0xc0000, 0xc8000);

    dprintf(1, "Turning on vga console\n");
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ax = 0x0003;
    call16_int(0x10, &br);

    // Write to screen.
    printf("Starting SeaBIOS\n\n");
}

void
optionrom_setup()
{
    dprintf(1, "Scan for option roms\n");
    rom_scan(0xc8000, 0xf0000);
}
