#ifndef __I440FX_H
#define __I440FX_H

#include "types.h" // u16

void piix_isa_bridge_init(u16 bdf, void *arg);
void piix_ide_init(u16 bdf, void *arg);
void piix4_pm_init(u16 bdf, void *arg);

#endif // __I440FX_H
