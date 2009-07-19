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
__callrom(struct rom_header *rom, u16 offset, u16 bdf)
{
    u16 seg = FLATPTR_TO_SEG(rom);
    dprintf(1, "Running option rom at %04x:%04x\n", seg, offset);

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

// Execute a given option rom at the standard entry vector.
static void
callrom(struct rom_header *rom, u16 bdf)
{
    __callrom(rom, OPTION_ROM_INITVECTOR, bdf);
}

// Execute a BCV option rom registered via add_bcv().
void
call_bcv(u16 seg, u16 ip)
{
    __callrom(MAKE_FLATPTR(seg, 0), ip, 0);
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
    struct pnp_data *pnp = (void*)((u8*)rom + rom->pnpoffset);
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
    pnp = (void*)((u8*)rom + pnp->nextoffset);
    if (pnp->signature != PNP_SIGNATURE)
        return NULL;
    return pnp;
}

// Check if a valid option rom has a pci struct; return it if so.
static struct pci_data *
get_pci_rom(struct rom_header *rom)
{
    struct pci_data *pci = (void*)((u32)rom + rom->pcioffset);
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
    return (void*)next_rom;
}

// Run rom init code and note rom size.
static int
init_optionrom(struct rom_header *rom, u16 bdf, int isvga)
{
    if (! is_valid_rom(rom))
        return -1;

    if (isvga || get_pnp_rom(rom))
        // Only init vga and PnP roms here.
        callrom(rom, bdf);

    next_rom = (u32)rom + ALIGN(rom->size * 512, OPTION_ROM_ALIGN);

    return 0;
}


/****************************************************************
 * Roms in CBFS
 ****************************************************************/

// Check if an option rom is at a hardcoded location or in CBFS.
static struct rom_header *
lookup_hardcode(u32 vendev)
{
    if (OPTIONROM_VENDEV_1
        && ((OPTIONROM_VENDEV_1 >> 16)
            | ((OPTIONROM_VENDEV_1 & 0xffff)) << 16) == vendev)
        return copy_rom((void*)OPTIONROM_MEM_1);
    if (OPTIONROM_VENDEV_2
        && ((OPTIONROM_VENDEV_2 >> 16)
            | ((OPTIONROM_VENDEV_2 & 0xffff)) << 16) == vendev)
        return copy_rom((void*)OPTIONROM_MEM_2);
    int ret = cbfs_copy_optionrom((void*)next_rom, BUILD_BIOS_ADDR - next_rom
                                  , vendev);
    if (ret < 0)
        return NULL;
    return (void*)next_rom;
}

// Run all roms in a given CBFS directory.
static void
run_cbfs_roms(const char *prefix, int isvga)
{
    struct cbfs_file *tmp = NULL;
    for (;;) {
        tmp = cbfs_copyfile_prefix(
            (void*)next_rom, BUILD_BIOS_ADDR - next_rom, prefix, tmp);
        if (!tmp)
            break;
        init_optionrom((void*)next_rom, 0, isvga);
    }
}


/****************************************************************
 * PCI roms
 ****************************************************************/

// Map the option rom of a given PCI device.
static struct rom_header *
map_pcirom(u16 bdf, u32 vendev)
{
    dprintf(6, "Attempting to map option rom on dev %02x:%02x.%x\n"
            , pci_bdf_to_bus(bdf), pci_bdf_to_dev(bdf), pci_bdf_to_fn(bdf));

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

    if (orig == sz || (u32)(orig + 4*1024*1024) < 20*1024*1024) {
        // Don't try to map to a pci addresses at its max, in the last
        // 4MiB of ram, or the first 16MiB of ram.
        dprintf(6, "Preset rom address doesn't look valid\n");
        goto fail;
    }

    // Looks like a rom - enable it.
    pci_config_writel(bdf, PCI_ROM_ADDRESS, orig | PCI_ROM_ADDRESS_ENABLE);

    struct rom_header *rom = (void*)orig;
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
        rom = (void*)((u32)rom + pci->ilen * 512);
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
static int
init_pcirom(u16 bdf, int isvga)
{
    u32 vendev = pci_config_readl(bdf, PCI_VENDOR_ID);
    dprintf(4, "Attempting to init PCI bdf %02x:%02x.%x (dev/ven %x)\n"
            , pci_bdf_to_bus(bdf), pci_bdf_to_dev(bdf), pci_bdf_to_fn(bdf)
            , vendev);
    struct rom_header *rom = lookup_hardcode(vendev);
    if (! rom)
        rom = map_pcirom(bdf, vendev);
    if (! rom)
        // No ROM present.
        return -1;
    return init_optionrom(rom, bdf, isvga);
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
            int ret = init_optionrom((void*)pos, 0, 0);
            if (ret)
                pos += OPTION_ROM_ALIGN;
            else
                pos = next_rom;
        }
    } else {
        // Find and deploy PCI roms.
        int bdf, max;
        foreachpci(bdf, max) {
            u16 v = pci_config_readw(bdf, PCI_CLASS_DEVICE);
            if (v == 0x0000 || v == 0xffff || v == PCI_CLASS_DISPLAY_VGA
                || (CONFIG_ATA && v == PCI_CLASS_STORAGE_IDE))
                continue;
            init_pcirom(bdf, 0);
        }

        // Find and deploy CBFS roms not associated with a device.
        run_cbfs_roms("genroms/", 0);
    }

    // All option roms found and deployed - now build BEV/BCV vectors.

    u32 pos = post_vga;
    while (pos < next_rom) {
        struct rom_header *rom = (void*)pos;
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
    VGAbdf = -1;
    next_rom = OPTION_ROM_START;

    if (CONFIG_OPTIONROMS_DEPLOYED) {
        // Option roms are already deployed on the system.
        init_optionrom((void*)OPTION_ROM_START, 0, 1);
    } else {
        // Find and deploy PCI VGA rom.
        int bdf = VGAbdf = pci_find_vga();
        if (bdf >= 0)
            init_pcirom(bdf, 1);

        // Find and deploy CBFS vga-style roms not associated with a device.
        run_cbfs_roms("vgaroms/", 1);
    }

    if (next_rom == OPTION_ROM_START) {
        // No VGA rom found
        next_rom += OPTION_ROM_ALIGN;
        return;
    }

    dprintf(1, "Turning on vga console\n");
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ax = 0x0003;
    call16_int(0x10, &br);

    // Write to screen.
    printf("Starting SeaBIOS\n\n");
}

void
s3_resume_vga_init()
{
    if (!CONFIG_S3_RESUME_VGA_INIT)
        return;
    struct rom_header *rom = (void*)OPTION_ROM_START;
    if (! is_valid_rom(rom))
        return;
    callrom(rom, 0);
}
