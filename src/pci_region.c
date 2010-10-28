// helper functions to manage pci io/memory/prefetch memory region
//
// Copyright (C) 2009 Isaku Yamahata <yamahata at valinux co jp>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.
//
//

#include "util.h"

#define PCI_REGION_DISABLED     (-1)

void pci_region_init(struct pci_region *r, u32 first, u32 last)
{
    r->first = first;
    r->last = last;

    r->cur_first = r->first;
}

// PCI_REGION_DISABLED represents that the region is in special state.
// its value is chosen such that cur_first can't be PCI_REGION_DISABLED
// normally.
// NOTE: the area right below 4G is used for LAPIC, so such area can't
//       be used for PCI memory.
u32 pci_region_disable(struct pci_region *r)
{
    return r->cur_first = PCI_REGION_DISABLED;
}

static int pci_region_disabled(const struct pci_region *r)
{
    return r->cur_first == PCI_REGION_DISABLED;
}

static u32 pci_region_alloc_align(struct pci_region *r, u32 size, u32 align)
{
    if (pci_region_disabled(r)) {
        return 0;
    }

    u32 s = ALIGN(r->cur_first, align);
    if (s > r->last || s < r->cur_first) {
        return 0;
    }
    u32 e = s + size;
    if (e < s || e - 1 > r->last) {
        return 0;
    }
    r->cur_first = e;
    return s;
}

u32 pci_region_alloc(struct pci_region *r, u32 size)
{
    return pci_region_alloc_align(r, size, size);
}

u32 pci_region_align(struct pci_region *r, u32 align)
{
    return pci_region_alloc_align(r, 0, align);
}

void pci_region_revert(struct pci_region *r, u32 addr)
{
    r->cur_first = addr;
}

u32 pci_region_addr(const struct pci_region *r)
{
    return r->cur_first;
}

u32 pci_region_size(const struct pci_region *r)
{
    return r->last - r->first + 1;
}
