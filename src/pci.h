#ifndef __PCI_H
#define __PCI_H

#include "types.h" // u32

typedef struct PCIDevice {
    u8 bus;
    u8 devfn;
} PCIDevice;

static inline PCIDevice pci_bd(u8 bus, u8 devfn) {
    struct PCIDevice d = {bus, devfn};
    return d;
}
static inline u16 pci_to_bdf(PCIDevice d) {
    return (d.bus << 8) | d.devfn;
}
static inline u8 pci_bdf_to_bus(u16 bdf) {
    return bdf >> 8;
}
static inline u8 pci_bdf_to_dev(u16 bdf) {
    return (bdf >> 3) & 0x1f;
}
static inline u8 pci_bdf_to_fn(u16 bdf) {
    return bdf & 0x07;
}

void pci_config_writel(PCIDevice d, u32 addr, u32 val);
void pci_config_writew(PCIDevice d, u32 addr, u16 val);
void pci_config_writeb(PCIDevice d, u32 addr, u8 val);
u32 pci_config_readl(PCIDevice d, u32 addr);
u16 pci_config_readw(PCIDevice d, u32 addr);
u8 pci_config_readb(PCIDevice d, u32 addr);

int pci_find_device(u16 vendid, u16 devid, int index, PCIDevice *dev);
int pci_find_classprog(u32 classprog, int index, PCIDevice *dev);
int pci_find_class(u16 classid, int index, PCIDevice *dev);

// pirtable.c
void create_pirtable();


/****************************************************************
 * PIR table
 ****************************************************************/

struct link_info {
    u8 link;
    u16 bitmap;
} PACKED;

struct pir_slot {
    u8 bus;
    u8 dev;
    struct link_info links[4];
    u8 slot_nr;
    u8 reserved;
} PACKED;

struct pir_header {
    u32 signature;
    u16 version;
    u16 size;
    u8 router_bus;
    u8 router_devfunc;
    u16 exclusive_irqs;
    u32 compatible_devid;
    u32 miniport_data;
    u8 reserved[11];
    u8 checksum;
    struct pir_slot slots[0];
} PACKED;

#define PIR_SIGNATURE 0x52495024 // $PIR


#endif
