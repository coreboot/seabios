#ifndef __PCI_H
#define __PCI_H

#include "types.h" // u32

static inline u8 pci_bdf_to_bus(u16 bdf) {
    return bdf >> 8;
}
static inline u8 pci_bdf_to_devfn(u16 bdf) {
    return bdf & 0xff;
}
static inline u8 pci_bdf_to_dev(u16 bdf) {
    return (bdf >> 3) & 0x1f;
}
static inline u8 pci_bdf_to_fn(u16 bdf) {
    return bdf & 0x07;
}

void pci_config_writel(u16 bdf, u32 addr, u32 val);
void pci_config_writew(u16 bdf, u32 addr, u16 val);
void pci_config_writeb(u16 bdf, u32 addr, u8 val);
u32 pci_config_readl(u16 bdf, u32 addr);
u16 pci_config_readw(u16 bdf, u32 addr);
u8 pci_config_readb(u16 bdf, u32 addr);

void pci_bus_setup();
int pci_find_device(u16 vendid, u16 devid, int start_bdf);
int pci_find_classprog(u32 classprog, int start_bdf);
int pci_find_class(u16 classid, int start_bdf);

extern int MaxBDF;

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
