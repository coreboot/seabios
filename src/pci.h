#ifndef __PCI_H
#define __PCI_H

#include "types.h" // u32

static inline u8 pci_bdf_to_bus(u16 bdf) {
    return bdf >> 8;
}
static inline u8 pci_bdf_to_devfn(u16 bdf) {
    return bdf & 0xff;
}
static inline u16 pci_bdf_to_busdev(u16 bdf) {
    return bdf & ~0x07;
}
static inline u8 pci_bdf_to_dev(u16 bdf) {
    return (bdf >> 3) & 0x1f;
}
static inline u8 pci_bdf_to_fn(u16 bdf) {
    return bdf & 0x07;
}
static inline u16 pci_to_bdf(int bus, int dev, int fn) {
    return (bus<<8) | (dev<<3) | fn;
}
static inline u16 pci_bus_devfn_to_bdf(int bus, u16 devfn) {
    return (bus << 8) | devfn;
}

static inline u32 pci_vd(u16 vendor, u16 device) {
    return (device << 16) | vendor;
}
static inline u16 pci_vd_to_ven(u32 vd) {
    return vd & 0xffff;
}
static inline u16 pci_vd_to_dev(u32 vd) {
    return vd >> 16;
}

void pci_config_writel(u16 bdf, u32 addr, u32 val);
void pci_config_writew(u16 bdf, u32 addr, u16 val);
void pci_config_writeb(u16 bdf, u32 addr, u8 val);
u32 pci_config_readl(u16 bdf, u32 addr);
u16 pci_config_readw(u16 bdf, u32 addr);
u8 pci_config_readb(u16 bdf, u32 addr);
void pci_config_maskw(u16 bdf, u32 addr, u16 off, u16 on);

int pci_find_vga(void);
int pci_find_device(u16 vendid, u16 devid);
int pci_find_class(u16 classid);

#define PP_ROOT      (1<<17)
#define PP_PCIBRIDGE (1<<18)
extern int *PCIpaths;
void pci_path_setup(void);

int pci_next(int bdf, int *pmax);
#define foreachpci(BDF, MAX)                    \
    for (MAX=0x0100, BDF=pci_next(0, &MAX)      \
         ; BDF >= 0                             \
         ; BDF=pci_next(BDF+1, &MAX))

#define foreachpci_in_bus(BDF, MAX, BUS)                                \
    for (MAX = pci_bus_devfn_to_bdf(BUS, 0) + 0x0100,                   \
         BDF = pci_next(pci_bus_devfn_to_bdf(BUS, 0), &MAX)             \
         ; BDF >= 0 && BDF < pci_bus_devfn_to_bdf(BUS, 0) + 0x0100      \
         ; MAX = pci_bus_devfn_to_bdf(BUS, 0) + 0x0100,                 \
           BDF = pci_next(BDF + 1, &MAX))

#define PCI_ANY_ID      (~0)
struct pci_device_id {
    u32 vendid;
    u32 devid;
    u32 class;
    u32 class_mask;
    void (*func)(u16 bdf, void *arg);
};

#define PCI_DEVICE(vendor_id, device_id, init_func)     \
    {                                                   \
        .vendid = (vendor_id),                          \
        .devid = (device_id),                           \
        .class = PCI_ANY_ID,                            \
        .class_mask = 0,                                \
        .func = (init_func)                             \
    }

#define PCI_DEVICE_CLASS(vendor_id, device_id, class_code, init_func)   \
    {                                                                   \
        .vendid = (vendor_id),                                          \
        .devid = (device_id),                                           \
        .class = (class_code),                                          \
        .class_mask = ~0,                                               \
        .func = (init_func)                                             \
    }

#define PCI_DEVICE_END                          \
    {                                           \
        .vendid = 0,                            \
    }

int pci_init_device(const struct pci_device_id *table, u16 bdf, void *arg);
int pci_find_init_device(const struct pci_device_id *ids, void *arg);
void pci_reboot(void);

// helper functions to access pci mmio bars from real mode
u32 pci_readl(u32 addr);
void pci_writel(u32 addr, u32 val);

// pirtable.c
void create_pirtable(void);


/****************************************************************
 * PIR table
 ****************************************************************/

extern u16 PirOffset;

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
