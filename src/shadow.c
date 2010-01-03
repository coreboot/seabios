// Support for enabling/disabling BIOS ram shadowing.
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
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
__make_bios_writable(u16 bdf)
{
    // Make ram from 0xc0000-0xf0000 writable
    int clear = 0;
    int i;
    for (i=0; i<6; i++) {
        int reg = pci_config_readb(bdf, 0x5a + i);
        if ((reg & 0x11) != 0x11) {
            // Need to copy optionroms to work around qemu implementation
            void *mem = (void*)(BUILD_ROM_START + i * 32*1024);
            memcpy((void*)BUILD_BIOS_TMP_ADDR, mem, 32*1024);
            pci_config_writeb(bdf, 0x5a + i, 0x33);
            memcpy(mem, (void*)BUILD_BIOS_TMP_ADDR, 32*1024);
            clear = 1;
        } else {
            pci_config_writeb(bdf, 0x5a + i, 0x33);
        }
    }
    if (clear)
        memset((void*)BUILD_BIOS_TMP_ADDR, 0, 32*1024);

    // Make ram from 0xf0000-0x100000 writable
    int reg = pci_config_readb(bdf, 0x59);
    pci_config_writeb(bdf, 0x59, 0x30);
    if (reg & 0x10)
        // Ram already present.
        return;

    // Copy bios.
    memcpy((void*)BUILD_BIOS_ADDR, (void*)BIOS_SRC_ADDR, BUILD_BIOS_SIZE);
}

// Make the 0xc0000-0x100000 area read/writable.
void
make_bios_writable(void)
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
    if (!(reg & 0x10)) {
        // QEMU doesn't fully implement the piix shadow capabilities -
        // if ram isn't backing the bios segment when shadowing is
        // disabled, the code itself wont be in memory.  So, run the
        // code from the high-memory flash location.
        u32 pos = (u32)__make_bios_writable - BUILD_BIOS_ADDR + BIOS_SRC_ADDR;
        void (*func)(u16 bdf) = (void*)pos;
        func(bdf);
        return;
    }
    // Ram already present - just enable writes
    __make_bios_writable(bdf);
}

// Make the BIOS code segment area (0xf0000) read-only.
void
make_bios_readonly(void)
{
    if (CONFIG_COREBOOT)
        return;

    dprintf(3, "locking shadow ram\n");

    int bdf = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82441);
    if (bdf < 0) {
        dprintf(1, "Unable to lock ram - bridge not found\n");
        return;
    }

    // Flush any pending writes before locking memory.
    wbinvd();

    // Write protect roms from 0xc0000-0xf0000
    int i;
    for (i=0; i<6; i++) {
        u32 mem = BUILD_ROM_START + i * 32*1024;
        if (RomEnd <= mem + 16*1024) {
            if (RomEnd > mem)
                pci_config_writeb(bdf, 0x5a + i, 0x31);
            break;
        }
        pci_config_writeb(bdf, 0x5a + i, 0x11);
    }

    // Write protect 0xf0000-0x100000
    pci_config_writeb(bdf, 0x59, 0x10);
}
