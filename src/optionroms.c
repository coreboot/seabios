// Option rom scanning code.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "bregs.h" // struct bregs
#include "farptr.h" // FLATPTR_TO_SEG
#include "config.h" // CONFIG_*
#include "util.h" // dprintf
#include "pci.h" // pci_find_class
#include "pci_regs.h" // PCI_ROM_ADDRESS
#include "pci_ids.h" // PCI_CLASS_DISPLAY_VGA
#include "boot.h" // IPL


/****************************************************************
 * Definitions
 ****************************************************************/

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

#define OPTION_ROM_START 0xc0000
#define OPTION_ROM_SIGNATURE 0xaa55
#define OPTION_ROM_ALIGN 2048
#define OPTION_ROM_INITVECTOR offsetof(struct rom_header, initVector[0])
#define PCI_ROM_SIGNATURE 0x52494350 // PCIR
#define PCIROM_CODETYPE_X86 0

// Next available position for an option rom.
static u32 next_rom;


/****************************************************************
 * Helper functions
 ****************************************************************/

// Execute a given option rom.
static void
callrom(struct rom_header *rom, u16 offset, u16 bdf)
{
    u16 seg = FLATPTR_TO_SEG(rom);
    dprintf(1, "Running option rom at %x:%x\n", seg, offset);

    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ax = bdf;
    br.bx = 0xffff;
    br.dx = 0xffff;
    br.es = SEG_BIOS;
    br.di = get_pnp_offset();
    br.cs = seg;
    br.ip = offset;
    call16big(&br);

    debug_serial_setup();
}

// Execute a BCV option rom registered via add_bcv().
void
call_bcv(u16 seg, u16 ip)
{
    callrom(MAKE_FLATPTR(seg, 0), ip, 0);
}

// Verify that an option rom looks valid
static int
is_valid_rom(struct rom_header *rom)
{
    dprintf(6, "Checking rom %p (sig %x size %d)\n"
            , rom, rom->signature, rom->size);
    if (rom->signature != OPTION_ROM_SIGNATURE)
        return 0;
    if (! rom->size)
        return 0;
    u32 len = rom->size * 512;
    u8 sum = checksum(rom, len);
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
    if (pnp->signature != PNP_SIGNATURE)
        return NULL;
    return pnp;
}

// Check for multiple pnp option rom headers.
static struct pnp_data *
get_pnp_next(struct rom_header *rom, struct pnp_data *pnp)
{
    if (! pnp->nextoffset)
        return NULL;
    pnp = (struct pnp_data *)((u8*)rom + pnp->nextoffset);
    if (pnp->signature != PNP_SIGNATURE)
        return NULL;
    return pnp;
}

// Check if a valid option rom has a pci struct; return it if so.
static struct pci_data *
get_pci_rom(struct rom_header *rom)
{
    struct pci_data *pci = (struct pci_data *)((u32)rom + rom->pcioffset);
    if (pci->signature != PCI_ROM_SIGNATURE)
        return NULL;
    return pci;
}

// Copy a rom to its permanent location below 1MiB
static struct rom_header *
copy_rom(struct rom_header *rom)
{
    u32 romsize = rom->size * 512;
    if (next_rom + romsize > BUILD_BIOS_ADDR) {
        // Option rom doesn't fit.
        dprintf(1, "Option rom %p doesn't fit.\n", rom);
        return NULL;
    }
    dprintf(4, "Copying option rom (size %d) from %p to %x\n"
            , romsize, rom, next_rom);
    memcpy((void*)next_rom, rom, romsize);
    return (struct rom_header *)next_rom;
}

// Check if an option rom is at a hardcoded location for a device.
static struct rom_header *
lookup_hardcode(u32 vendev)
{
    if (OPTIONROM_VENDEV_1
        && ((OPTIONROM_VENDEV_1 >> 16)
            | ((OPTIONROM_VENDEV_1 & 0xffff)) << 16) == vendev)
        return copy_rom((struct rom_header *)OPTIONROM_MEM_1);
    if (OPTIONROM_VENDEV_2
        && ((OPTIONROM_VENDEV_2 >> 16)
            | ((OPTIONROM_VENDEV_2 & 0xffff)) << 16) == vendev)
        return copy_rom((struct rom_header *)OPTIONROM_MEM_2);
    struct rom_header *rom = cb_find_optionrom(vendev);
    if (rom)
        return copy_rom(rom);
    return NULL;
}

// Map the option rom of a given PCI device.
static struct rom_header *
map_optionrom(u16 bdf, u32 vendev)
{
    dprintf(6, "Attempting to map option rom on dev %x\n", bdf);

    u8 htype = pci_config_readb(bdf, PCI_HEADER_TYPE);
    if ((htype & 0x7f) != PCI_HEADER_TYPE_NORMAL) {
        dprintf(6, "Skipping non-normal pci device (type=%x)\n", htype);
        return NULL;
    }

    u32 orig = pci_config_readl(bdf, PCI_ROM_ADDRESS);
    pci_config_writel(bdf, PCI_ROM_ADDRESS, ~PCI_ROM_ADDRESS_ENABLE);
    u32 sz = pci_config_readl(bdf, PCI_ROM_ADDRESS);

    dprintf(6, "Option rom sizing returned %x %x\n", orig, sz);
    orig &= ~PCI_ROM_ADDRESS_ENABLE;
    if (!sz || sz == 0xffffffff)
        goto fail;

    if (orig < 16*1024*1024) {
        dprintf(6, "Preset rom address doesn't look valid\n");
        goto fail;
    }

    // Looks like a rom - enable it.
    pci_config_writel(bdf, PCI_ROM_ADDRESS, orig | PCI_ROM_ADDRESS_ENABLE);

    struct rom_header *rom = (struct rom_header *)orig;
    for (;;) {
        dprintf(5, "Inspecting possible rom at %p (dv=%x bdf=%x)\n"
                , rom, vendev, bdf);
        if (rom->signature != OPTION_ROM_SIGNATURE) {
            dprintf(6, "No option rom signature (got %x)\n", rom->signature);
            goto fail;
        }
        struct pci_data *pci = get_pci_rom(rom);
        if (! pci) {
            dprintf(6, "No valid pci signature found\n");
            goto fail;
        }

        u32 vd = (pci->device << 16) | pci->vendor;
        if (vd == vendev && pci->type == PCIROM_CODETYPE_X86)
            // A match
            break;
        dprintf(6, "Didn't match dev/ven (got %x) or type (got %d)\n"
                , vd, pci->type);
        if (pci->indicator & 0x80) {
            dprintf(6, "No more images left\n");
            goto fail;
        }
        rom = (struct rom_header *)((u32)rom + pci->ilen * 512);
    }

    rom = copy_rom(rom);
    pci_config_writel(bdf, PCI_ROM_ADDRESS, orig);
    return rom;
fail:
    // Not valid - restore original and exit.
    pci_config_writel(bdf, PCI_ROM_ADDRESS, orig);
    return NULL;
}

// Attempt to map and initialize the option rom on a given PCI device.
static struct rom_header *
init_optionrom(u16 bdf)
{
    u32 vendev = pci_config_readl(bdf, PCI_VENDOR_ID);
    dprintf(4, "Attempting to init PCI bdf %x (dev/ven %x)\n", bdf, vendev);
    struct rom_header *rom = lookup_hardcode(vendev);
    if (! rom)
        rom = map_optionrom(bdf, vendev);
    if (! rom)
        // No ROM present.
        return NULL;

    if (! is_valid_rom(rom))
        return NULL;

    if (get_pnp_rom(rom))
        // Init the PnP rom.
        callrom(rom, OPTION_ROM_INITVECTOR, bdf);

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
        int bdf, max;
        foreachpci(bdf, max) {
            u16 v = pci_config_readw(bdf, PCI_CLASS_DEVICE);
            if (v == 0x0000 || v == 0xffff || v == PCI_CLASS_DISPLAY_VGA
                || (CONFIG_ATA && v == PCI_CLASS_STORAGE_IDE))
                continue;
            init_optionrom(bdf);
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
            // Legacy rom.
            add_bcv(FLATPTR_TO_SEG(rom), OPTION_ROM_INITVECTOR, 0);
            continue;
        }
        // PnP rom.
        if (pnp->bev)
            // Can boot system - add to IPL list.
            add_bev(FLATPTR_TO_SEG(rom), pnp->bev, pnp->productname);
        else
            // Check for BCV (there may be multiple).
            while (pnp && pnp->bcv) {
                add_bcv(FLATPTR_TO_SEG(rom), pnp->bcv, pnp->productname);
                pnp = get_pnp_next(rom, pnp);
            }
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
        int bdf = pci_find_class(PCI_CLASS_DISPLAY_VGA);
        if (bdf < 0)
            // Device not found
            return;

        struct rom_header *rom = init_optionrom(bdf);
        if (rom && !get_pnp_rom(rom))
            // Call rom even if it isn't a pnp rom.
            callrom(rom, OPTION_ROM_INITVECTOR, bdf);
    }

    dprintf(1, "Turning on vga console\n");
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ax = 0x0003;
    call16_int(0x10, &br);

    // Write to screen.
    printf("Starting SeaBIOS\n\n");
}
