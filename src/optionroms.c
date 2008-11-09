// Option rom scanning code.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "bregs.h" // struct bregs
#include "biosvar.h" // struct ipl_entry_s
#include "util.h" // dprintf
#include "pci.h" // pci_find_class
#include "pci_regs.h" // PCI_ROM_ADDRESS
#include "pci_ids.h" // PCI_CLASS_DISPLAY_VGA


/****************************************************************
 * Definitions
 ****************************************************************/

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

#define OPTIONROM_BDF_1 0x0000
#define OPTIONROM_MEM_1 0x00000000
#define OPTIONROM_BDF_2 0x0000
#define OPTIONROM_MEM_2 0x00000000

#define OPTION_ROM_START 0xc0000
#define OPTION_ROM_SIGNATURE 0xaa55
#define OPTION_ROM_ALIGN 2048
#define OPTION_ROM_INITVECTOR offsetof(struct rom_header, initVector[0])

// Next available position for an option rom.
static u32 next_rom;


/****************************************************************
 * Helper functions
 ****************************************************************/

// Execute a given option rom.
static void
callrom(struct rom_header *rom, u16 offset, u16 bdf)
{
    u16 seg = FARPTR_TO_SEG(rom);
    dprintf(1, "Running option rom at %x:%x\n", seg, offset);

    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ax = bdf;
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

// Verify that an option rom looks valid
static int
is_valid_rom(struct rom_header *rom)
{
    if (rom->signature != OPTION_ROM_SIGNATURE)
        return 0;
    u32 len = rom->size * 512;
    u8 sum = checksum((void*)rom, len);
    if (sum != 0) {
        dprintf(1, "Found option rom with bad checksum: loc=%p len=%d sum=%x\n"
                , rom, len, sum);
        return 0;
    }
    return 1;
}

// Check if a valid option rom has a pnp struct; return it if so.
static struct pnp_data *
get_pnp_rom(struct rom_header *rom)
{
    struct pnp_data *pnp = (struct pnp_data *)((u8*)rom + rom->pnpoffset);
    if (pnp->signature != *(u32*)pnp_string)
        return NULL;
    return pnp;
}

// Add a BEV vector for a given pnp compatible option rom.
static void
add_ipl(struct rom_header *rom, struct pnp_data *pnp)
{
#define ebda ((struct extended_bios_data_area_s *)MAKE_FARPTR(SEG_EBDA, 0))

    // Found a device that thinks it can boot the system.  Record
    // its BEV and product name string.

    if (! CONFIG_BOOT)
        return;

    if (ebda->ipl.count >= ARRAY_SIZE(ebda->ipl.table))
        return;

    struct ipl_entry_s *ip = &ebda->ipl.table[ebda->ipl.count];
    ip->type = IPL_TYPE_BEV;
    ip->vector = (FARPTR_TO_SEG(rom) << 16) | pnp->bev;

    u16 desc = pnp->productname;
    if (desc)
        ip->description = MAKE_FARPTR(FARPTR_TO_SEG(rom), desc);

    ebda->ipl.count++;
}

// Check if an option rom is at a hardcoded location for a device.
static struct rom_header *
lookup_hardcode(PCIDevice d)
{
    if (OPTIONROM_BDF_1
        && OPTIONROM_BDF_1 == pci_to_bdf(d))
        return (struct rom_header *)OPTIONROM_MEM_1;
    else if (OPTIONROM_BDF_2
        && OPTIONROM_BDF_2 == pci_to_bdf(d))
        return (struct rom_header *)OPTIONROM_MEM_2;
    // XXX - check LAR when in coreboot?
    return NULL;
}

// Map the option rom of a given PCI device.
static struct rom_header *
map_optionrom(PCIDevice d)
{
    u32 orig = pci_config_readl(d, PCI_ROM_ADDRESS);
    pci_config_writel(d, PCI_ROM_ADDRESS, ~PCI_ROM_ADDRESS_ENABLE);
    u32 sz = pci_config_readl(d, PCI_ROM_ADDRESS);

    if (!sz || sz == 0xffffffff)
        goto fail;

    // Looks like a rom - map it to just above end of memory.
    u32 mappos = ALIGN(GET_EBDA(ram_size), OPTION_ROM_ALIGN);
    pci_config_writel(d, PCI_ROM_ADDRESS, mappos | PCI_ROM_ADDRESS_ENABLE);

    struct rom_header *rom = (struct rom_header *)mappos;
    if (rom->signature != OPTION_ROM_SIGNATURE)
        goto fail;

    return rom;
fail:
    // Not valid - restore original and exit.
    pci_config_writel(d, PCI_ROM_ADDRESS, orig);
    return NULL;
}

// Attempt to map and initialize the option rom on a given PCI device.
static struct rom_header *
init_optionrom(PCIDevice d)
{
    struct rom_header *rom = lookup_hardcode(d);
    if (! rom)
        rom = map_optionrom(d);
    if (! rom)
        // No ROM present.
        return NULL;

    u32 romsize = rom->size * 512;
    if (next_rom + romsize > BUILD_BIOS_ADDR) {
        // Option rom doesn't fit.
        dprintf(1, "Option rom %x doesn't fit.", pci_to_bdf(d));
        pci_config_writel(d, PCI_ROM_ADDRESS, next_rom);
        return NULL;
    }
    memcpy((void*)next_rom, rom, romsize);
    pci_config_writel(d, PCI_ROM_ADDRESS, next_rom);
    rom = (struct rom_header *)next_rom;

    if (! is_valid_rom(rom))
        return NULL;

    if (get_pnp_rom(rom))
        // Init the PnP rom.
        callrom(rom, OPTION_ROM_INITVECTOR, pci_to_bdf(d));

    next_rom += ALIGN(rom->size * 512, OPTION_ROM_ALIGN);

    return rom;
}


/****************************************************************
 * Non-VGA option rom init
 ****************************************************************/

void
optionrom_setup()
{
    if (! CONFIG_OPTIONROMS)
        return;

    dprintf(1, "Scan for option roms\n");

    u32 post_vga = next_rom;

    if (CONFIG_OPTIONROMS_DEPLOYED) {
        // Option roms are already deployed on the system.
        u32 pos = next_rom;
        while (pos < BUILD_BIOS_ADDR) {
            struct rom_header *rom = (struct rom_header *)pos;
            if (! is_valid_rom(rom)) {
                pos += OPTION_ROM_ALIGN;
                continue;
            }
            if (get_pnp_rom(rom))
                callrom(rom, OPTION_ROM_INITVECTOR, 0);
            pos += ALIGN(rom->size * 512, OPTION_ROM_ALIGN);
            next_rom = pos;
        }
    } else {
        // Find and deploy PCI roms.
        int devfn, bus;
        for (bus=0; bus < CONFIG_PCI_BUS_COUNT; bus++) {
            for (devfn=0; devfn<0x100; devfn++) {
                PCIDevice d = pci_bd(bus, devfn);
                u16 v = pci_config_readw(d, PCI_CLASS_DEVICE);
                if (v == 0x0000 || v == 0xffff || v == PCI_CLASS_DISPLAY_VGA)
                    continue;
                init_optionrom(d);
            }
        }
    }

    // All option roms found and deployed - now build BEV/BCV vectors.

    u32 pos = post_vga;
    while (pos < next_rom) {
        struct rom_header *rom = (struct rom_header *)pos;
        if (! is_valid_rom(rom)) {
            pos += OPTION_ROM_ALIGN;
            continue;
        }
        pos += ALIGN(rom->size * 512, OPTION_ROM_ALIGN);
        struct pnp_data *pnp = get_pnp_rom(rom);
        if (! pnp) {
            // Legacy rom - run init vector now.
            callrom(rom, OPTION_ROM_INITVECTOR, 0);
            continue;
        }
        // PnP rom.
        if (pnp->bev)
            // Can boot system - add to IPL list.
            add_ipl(rom, pnp);
        else if (pnp->bcv)
            // Has BCV - run it now.
            callrom(rom, pnp->bcv, 0);
    }
}


/****************************************************************
 * VGA init
 ****************************************************************/

// Call into vga code to turn on console.
void
vga_setup()
{
    if (! CONFIG_OPTIONROMS)
        return;

    dprintf(1, "Scan for VGA option rom\n");
    next_rom = OPTION_ROM_START;

    if (CONFIG_OPTIONROMS_DEPLOYED) {
        // Option roms are already deployed on the system.
        struct rom_header *rom = (struct rom_header *)OPTION_ROM_START;
        if (! is_valid_rom(rom))
            return;
        callrom(rom, OPTION_ROM_INITVECTOR, 0);
        next_rom += ALIGN(rom->size * 512, OPTION_ROM_ALIGN);
    } else {
        // Find and deploy PCI VGA rom.
        PCIDevice d;
        int ret = pci_find_class(PCI_CLASS_DISPLAY_VGA, 0, &d);
        if (ret)
            // Device not found
            return;

        struct rom_header *rom = init_optionrom(d);
        if (rom && !get_pnp_rom(rom))
            // Call rom even if it isn't a pnp rom.
            callrom(rom, OPTION_ROM_INITVECTOR, pci_to_bdf(d));
    }

    dprintf(1, "Turning on vga console\n");
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ax = 0x0003;
    call16_int(0x10, &br);

    // Write to screen.
    printf("Starting SeaBIOS\n\n");
}
