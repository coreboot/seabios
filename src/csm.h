#ifndef __CSM_H
#define __CSM_H

#include "types.h"
#include "pci.h"

#define UINT8 u8
#define UINT16 u16
#define UINT32 u32

// csm.c
int csm_bootprio_fdc(struct pci_device *pci, int port, int fdid);
int csm_bootprio_ata(struct pci_device *pci, int chanid, int slave);
int csm_bootprio_pci(struct pci_device *pci);

#include "LegacyBios.h"

#endif // __CSM_H
