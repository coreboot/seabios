// Support for enabling/disabling BIOS ram shadowing.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // memcpy
#include "pci.h" // pci_config_writeb
#include "config.h" // CONFIG_*
#include "pci_ids.h" // PCI_VENDOR_ID_INTEL

// Test if 'addr' is in the range from 'start'..'start+size'
#define IN_RANGE(addr, start, size) ({   \
            u32 __addr = (addr);         \
            u32 __start = (start);       \
            u32 __size = (size);         \
            (__addr - __start < __size); \
        })

// On the emulators, the bios at 0xf0000 is also at 0xffff0000
#define BIOS_SRC_ADDR 0xffff0000

// Enable shadowing and copy bios.
static void
copy_bios(u16 bdf, int reg)
{
    pci_config_writeb(bdf, 0x59, reg | 0x30);
    memcpy((void*)BUILD_BIOS_ADDR, (void*)BIOS_SRC_ADDR, BUILD_BIOS_SIZE);
}

// Make the BIOS code segment area (0xf0000) writable.
void
make_bios_writable()
{
    if (CONFIG_COREBOOT)
        return;

    dprintf(3, "enabling shadow ram\n");

    // Locate chip controlling ram shadowing.
    int bdf = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82441);
    if (bdf < 0) {
        dprintf(1, "Unable to unlock ram - bridge not found\n");
        return;
    }

    int reg = pci_config_readb(bdf, 0x59);
    if (reg & 0x30) {
        // Ram already present - just enable writes
        pci_config_writeb(bdf, 0x59, reg | 0x30);
        return;
    }

    // Enable shadowing and copy bios.
    if (IN_RANGE((u32)copy_bios, BUILD_BIOS_ADDR, BUILD_BIOS_SIZE)) {
        // Jump to shadow enable function - use the copy in the
        // temporary storage area so that memory does not change under
        // the executing code.
        u32 pos = (u32)copy_bios - BUILD_BIOS_ADDR + BIOS_SRC_ADDR;
        void (*func)(u16 bdf, int reg) = (void*)pos;
        func(bdf, reg);
    } else {
        copy_bios(bdf, reg);
    }
}

// Make the BIOS code segment area (0xf0000) read-only.
void
make_bios_readonly()
{
    if (CONFIG_COREBOOT)
        return;

    dprintf(3, "locking shadow ram\n");

    int bdf = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82441);
    if (bdf < 0) {
        dprintf(1, "Unable to lock ram - bridge not found\n");
        return;
    }

    wbinvd();
    int v = pci_config_readb(bdf, 0x59);
    v = (v & 0x0f) | (0x10);
    pci_config_writeb(bdf, 0x59, v);
}
