#ifndef __PCI_H
#define __PCI_H

#include "types.h" // u32

typedef struct PCIDevice {
    u8 bus;
    u8 devfn;
} PCIDevice;

static inline PCIDevice
pci_bd(u8 bus, u8 devfn)
{
    struct PCIDevice d = {bus, devfn};
    return d;
}

void pci_config_writel(PCIDevice d, u32 addr, u32 val);
void pci_config_writew(PCIDevice d, u32 addr, u16 val);
void pci_config_writeb(PCIDevice d, u32 addr, u8 val);
u32 pci_config_readl(PCIDevice d, u32 addr);
u16 pci_config_readw(PCIDevice d, u32 addr);
u8 pci_config_readb(PCIDevice d, u32 addr);

int pci_find_device(u16 vendid, u16 devid, int index, PCIDevice *dev);
int pci_find_class(u32 classid, int index, PCIDevice *dev);

// pirtable.c
void create_pirtable();


/****************************************************************
 * PCI definitions
 ****************************************************************/

#define PCI_VENDOR_ID		0x00	/* 16 bits */
#define PCI_DEVICE_ID		0x02	/* 16 bits */
#define PCI_COMMAND		0x04	/* 16 bits */
#define  PCI_COMMAND_IO		0x1	/* Enable response in I/O space */
#define  PCI_COMMAND_MEMORY	0x2	/* Enable response in Memory space */
#define PCI_CLASS_DEVICE        0x0a    /* Device class */
#define PCI_INTERRUPT_LINE	0x3c	/* 8 bits */
#define PCI_INTERRUPT_PIN	0x3d	/* 8 bits */
#define PCI_MIN_GNT		0x3e	/* 8 bits */
#define PCI_MAX_LAT		0x3f	/* 8 bits */


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
