// PCI config space access functions.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "pci.h" // pci_config_writel
#include "ioport.h" // outl
#include "util.h" // dprintf
#include "config.h" // CONFIG_*
#include "farptr.h" // CONFIG_*
#include "pci_regs.h" // PCI_VENDOR_ID
#include "pci_ids.h" // PCI_CLASS_DISPLAY_VGA

void pci_config_writel(u16 bdf, u32 addr, u32 val)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc), PORT_PCI_CMD);
    outl(val, PORT_PCI_DATA);
}

void pci_config_writew(u16 bdf, u32 addr, u16 val)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc), PORT_PCI_CMD);
    outw(val, PORT_PCI_DATA + (addr & 2));
}

void pci_config_writeb(u16 bdf, u32 addr, u8 val)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc), PORT_PCI_CMD);
    outb(val, PORT_PCI_DATA + (addr & 3));
}

u32 pci_config_readl(u16 bdf, u32 addr)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc), PORT_PCI_CMD);
    return inl(PORT_PCI_DATA);
}

u16 pci_config_readw(u16 bdf, u32 addr)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc), PORT_PCI_CMD);
    return inw(PORT_PCI_DATA + (addr & 2));
}

u8 pci_config_readb(u16 bdf, u32 addr)
{
    outl(0x80000000 | (bdf << 8) | (addr & 0xfc), PORT_PCI_CMD);
    return inb(PORT_PCI_DATA + (addr & 3));
}

void
pci_config_maskw(u16 bdf, u32 addr, u16 off, u16 on)
{
    u16 val = pci_config_readw(bdf, addr);
    val = (val & ~off) | on;
    pci_config_writew(bdf, addr, val);
}

// Helper function for foreachbdf() macro - return next device
int
pci_next(int bdf, int *pmax)
{
    if (pci_bdf_to_fn(bdf) == 1
        && (pci_config_readb(bdf-1, PCI_HEADER_TYPE) & 0x80) == 0)
        // Last found device wasn't a multi-function device - skip to
        // the next device.
        bdf += 7;

    int max = *pmax;
    for (;;) {
        if (bdf >= max) {
            if (CONFIG_PCI_ROOT1 && bdf <= (CONFIG_PCI_ROOT1 << 8))
                bdf = CONFIG_PCI_ROOT1 << 8;
            else if (CONFIG_PCI_ROOT2 && bdf <= (CONFIG_PCI_ROOT2 << 8))
                bdf = CONFIG_PCI_ROOT2 << 8;
            else
            	return -1;
            *pmax = max = bdf + 0x0100;
        }

        u16 v = pci_config_readw(bdf, PCI_VENDOR_ID);
        if (v != 0x0000 && v != 0xffff)
            // Device is present.
            break;

        if (pci_bdf_to_fn(bdf) == 0)
            bdf += 8;
        else
            bdf += 1;
    }

    // Check if found device is a bridge.
    u32 v = pci_config_readb(bdf, PCI_HEADER_TYPE);
    v &= 0x7f;
    if (v == PCI_HEADER_TYPE_BRIDGE || v == PCI_HEADER_TYPE_CARDBUS) {
        v = pci_config_readl(bdf, PCI_PRIMARY_BUS);
        int newmax = (v & 0xff00) + 0x0100;
        if (newmax > max)
            *pmax = newmax;
    }

    return bdf;
}

struct pci_device *PCIDevices;
int MaxPCIBus;

void
pci_probe(void)
{
    int rootbuses = 0;
    struct pci_device *busdevs[256];
    memset(busdevs, 0, sizeof(busdevs));

    struct pci_device **pprev = &PCIDevices;
    u8 lastbus = 0;
    int bdf, max;
    foreachbdf(bdf, max) {
        // Create new pci_device struct and add to list.
        struct pci_device *dev = malloc_tmp(sizeof(*dev));
        if (!dev) {
            warn_noalloc();
            return;
        }
        memset(dev, 0, sizeof(*dev));
        *pprev = dev;
        pprev = &dev->next;

        // Find parent device.
        u8 bus = pci_bdf_to_bus(bdf), rootbus;
        struct pci_device *parent = busdevs[bus];
        if (!parent) {
            if (bus != lastbus)
                rootbuses++;
            lastbus = bus;
            rootbus = rootbuses;
        } else {
            rootbus = parent->rootbus;
        }
        if (bus > MaxPCIBus)
            MaxPCIBus = bus;

        // Populate pci_device info.
        dev->bdf = bdf;
        dev->parent = parent;
        dev->rootbus = rootbus;
        u32 vendev = pci_config_readl(bdf, PCI_VENDOR_ID);
        dev->vendor = vendev & 0xffff;
        dev->device = vendev >> 16;
        u32 classrev = pci_config_readl(bdf, PCI_CLASS_REVISION);
        dev->class = classrev >> 16;
        dev->prog_if = classrev >> 8;
        dev->revision = classrev & 0xff;
        dev->header_type = pci_config_readb(bdf, PCI_HEADER_TYPE);
        u8 v = dev->header_type & 0x7f;
        if (v == PCI_HEADER_TYPE_BRIDGE || v == PCI_HEADER_TYPE_CARDBUS) {
            u8 secbus = pci_config_readb(bdf, PCI_SECONDARY_BUS);
            dev->secondary_bus = secbus;
            if (secbus > bus && !busdevs[secbus])
                busdevs[secbus] = dev;
        }
    }
}

// Search for a device with the specified vendor and device ids.
int
pci_find_device(u16 vendid, u16 devid)
{
    u32 id = (devid << 16) | vendid;
    int bdf, max;
    foreachbdf(bdf, max) {
        u32 v = pci_config_readl(bdf, PCI_VENDOR_ID);
        if (v == id)
            return bdf;
    }
    return -1;
}

// Search for a device with the specified class id.
int
pci_find_class(u16 classid)
{
    int bdf, max;
    foreachbdf(bdf, max) {
        u16 v = pci_config_readw(bdf, PCI_CLASS_DEVICE);
        if (v == classid)
            return bdf;
    }
    return -1;
}

int pci_init_device(const struct pci_device_id *ids, u16 bdf, void *arg)
{
    u16 vendor_id = pci_config_readw(bdf, PCI_VENDOR_ID);
    u16 device_id = pci_config_readw(bdf, PCI_DEVICE_ID);
    u16 class = pci_config_readw(bdf, PCI_CLASS_DEVICE);

    while (ids->vendid || ids->class_mask) {
        if ((ids->vendid == PCI_ANY_ID || ids->vendid == vendor_id) &&
            (ids->devid == PCI_ANY_ID || ids->devid == device_id) &&
            !((ids->class ^ class) & ids->class_mask)) {
            if (ids->func) {
                ids->func(bdf, arg);
            }
            return 0;
        }
        ids++;
    }
    return -1;
}

int pci_find_init_device(const struct pci_device_id *ids, void *arg)
{
    int bdf, max;

    foreachbdf(bdf, max) {
        if (pci_init_device(ids, bdf, arg) == 0) {
            return bdf;
        }
    }
    return -1;
}

void
pci_reboot(void)
{
    u8 v = inb(PORT_PCI_REBOOT) & ~6;
    outb(v|2, PORT_PCI_REBOOT); /* Request hard reset */
    udelay(50);
    outb(v|6, PORT_PCI_REBOOT); /* Actually do the reset */
    udelay(50);
}

// helper functions to access pci mmio bars from real mode

u32 VISIBLE32FLAT
pci_readl_32(u32 addr)
{
    dprintf(3, "32: pci read : %x\n", addr);
    return readl((void*)addr);
}

u32 pci_readl(u32 addr)
{
    if (MODESEGMENT) {
        dprintf(3, "16: pci read : %x\n", addr);
        extern void _cfunc32flat_pci_readl_32(u32 addr);
        return call32(_cfunc32flat_pci_readl_32, addr, -1);
    } else {
        return pci_readl_32(addr);
    }
}

struct reg32 {
    u32 addr;
    u32 data;
};

void VISIBLE32FLAT
pci_writel_32(struct reg32 *reg32)
{
    dprintf(3, "32: pci write: %x, %x (%p)\n", reg32->addr, reg32->data, reg32);
    writel((void*)(reg32->addr), reg32->data);
}

void pci_writel(u32 addr, u32 val)
{
    struct reg32 reg32 = { .addr = addr, .data = val };
    if (MODESEGMENT) {
        dprintf(3, "16: pci write: %x, %x (%x:%p)\n",
                reg32.addr, reg32.data, GET_SEG(SS), &reg32);
        void *flatptr = MAKE_FLATPTR(GET_SEG(SS), &reg32);
        extern void _cfunc32flat_pci_writel_32(struct reg32 *reg32);
        call32(_cfunc32flat_pci_writel_32, (u32)flatptr, -1);
    } else {
        pci_writel_32(&reg32);
    }
}
