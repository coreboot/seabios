#ifndef __I440FX_H
#define __I440FX_H

#include "types.h" // u16

void i440fx_bios_make_writable(u16 bdf, void *arg);
void i440fx_bios_make_readonly(u16 bdf, void *arg);
void piix_isa_bridge_init(u16 bdf, void *arg);
void piix_ide_init(u16 bdf, void *arg);
void piix4_pm_init(u16 bdf, void *arg);
void piix4_fadt_init(u16 bdf, void *arg);
void piix4_apmc_smm_init(u16 bdf, void *arg);

#endif // __I440FX_H
