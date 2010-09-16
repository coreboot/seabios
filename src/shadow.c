// Support for enabling/disabling BIOS ram shadowing.
//
// Copyright (C) 2008-2010  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // memcpy
#include "pci.h" // pci_config_writeb
#include "config.h" // CONFIG_*
#include "pci_ids.h" // PCI_VENDOR_ID_INTEL
#include "dev-i440fx.h"

// On the emulators, the bios at 0xf0000 is also at 0xffff0000
#define BIOS_SRC_OFFSET 0xfff00000

// Enable shadowing and copy bios.
static void
__make_bios_writable_intel(u16 bdf, u32 pam0)
{
    // Make ram from 0xc0000-0xf0000 writable
    int clear = 0;
    int i;
    for (i=0; i<6; i++) {
        u32 pam = pam0 + 1 + i;
        int reg = pci_config_readb(bdf, pam);
        if (CONFIG_OPTIONROMS_DEPLOYED && (reg & 0x11) != 0x11) {
            // Need to copy optionroms to work around qemu implementation
            void *mem = (void*)(BUILD_ROM_START + i * 32*1024);
            memcpy((void*)BUILD_BIOS_TMP_ADDR, mem, 32*1024);
            pci_config_writeb(bdf, pam, 0x33);
            memcpy(mem, (void*)BUILD_BIOS_TMP_ADDR, 32*1024);
            clear = 1;
        } else {
            pci_config_writeb(bdf, pam, 0x33);
        }
    }
    if (clear)
        memset((void*)BUILD_BIOS_TMP_ADDR, 0, 32*1024);

    // Make ram from 0xf0000-0x100000 writable
    int reg = pci_config_readb(bdf, pam0);
    pci_config_writeb(bdf, pam0, 0x30);
    if (reg & 0x10)
        // Ram already present.
        return;

    // Copy bios.
    extern u8 code32flat_start[], code32flat_end[];
    memcpy(code32flat_start, code32flat_start + BIOS_SRC_OFFSET
           , code32flat_end - code32flat_start);
}

void
make_bios_writable_intel(u16 bdf, u32 pam0)
{
    int reg = pci_config_readb(bdf, pam0);
    if (!(reg & 0x10)) {
        // QEMU doesn't fully implement the piix shadow capabilities -
        // if ram isn't backing the bios segment when shadowing is
        // disabled, the code itself wont be in memory.  So, run the
        // code from the high-memory flash location.
        u32 pos = (u32)__make_bios_writable_intel + BIOS_SRC_OFFSET;
        void (*func)(u16 bdf, u32 pam0) = (void*)pos;
        func(bdf, pam0);
        return;
    }
    // Ram already present - just enable writes
    __make_bios_writable_intel(bdf, pam0);
}

void
make_bios_readonly_intel(u16 bdf, u32 pam0)
{
    // Flush any pending writes before locking memory.
    wbinvd();

    // Write protect roms from 0xc0000-0xf0000
    int i;
    for (i=0; i<6; i++) {
        u32 mem = BUILD_ROM_START + i * 32*1024;
        u32 pam = pam0 + 1 + i;
        if (RomEnd <= mem + 16*1024) {
            if (RomEnd > mem)
                pci_config_writeb(bdf, pam, 0x31);
            break;
        }
        pci_config_writeb(bdf, pam, 0x11);
    }

    // Write protect 0xf0000-0x100000
    pci_config_writeb(bdf, pam0, 0x10);
}

static const struct pci_device_id dram_controller_make_writable_tbl[] = {
    PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82441,
               i440fx_bios_make_writable),
    PCI_DEVICE_END
};

// Make the 0xc0000-0x100000 area read/writable.
void
make_bios_writable(void)
{
    if (CONFIG_COREBOOT)
        return;

    dprintf(3, "enabling shadow ram\n");

    // at this point, statically allocated variables can't be written.
    // so stack should be used.

    // Locate chip controlling ram shadowing.
    int bdf = pci_find_init_device(dram_controller_make_writable_tbl, NULL);
    if (bdf < 0) {
        dprintf(1, "Unable to unlock ram - bridge not found\n");
    }
}

static const struct pci_device_id dram_controller_make_readonly_tbl[] = {
    PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82441,
               i440fx_bios_make_readonly),
    PCI_DEVICE_END
};

// Make the BIOS code segment area (0xf0000) read-only.
void
make_bios_readonly(void)
{
    if (CONFIG_COREBOOT)
        return;

    dprintf(3, "locking shadow ram\n");
    int bdf = pci_find_init_device(dram_controller_make_readonly_tbl, NULL);
    if (bdf < 0) {
        dprintf(1, "Unable to lock ram - bridge not found\n");
    }
}

void
qemu_prep_reset(void)
{
    if (CONFIG_COREBOOT)
        return;
    // QEMU doesn't map 0xc0000-0xfffff back to the original rom on a
    // reset, so do that manually before invoking a hard reset.
    make_bios_writable();
    extern u8 code32flat_start[], code32flat_end[];
    memcpy(code32flat_start, code32flat_start + BIOS_SRC_OFFSET
           , code32flat_end - code32flat_start);
}
