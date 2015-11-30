#ifndef TCGBIOS_H
#define TCGBIOS_H

#include "types.h"

struct bregs;
void tpm_interrupt_handler32(struct bregs *regs);

void tpm_setup(void);
void tpm_prepboot(void);
void tpm_s3_resume(void);
u32 tpm_add_bcv(u32 bootdrv, const u8 *addr, u32 length);
u32 tpm_add_cdrom(u32 bootdrv, const u8 *addr, u32 length);
u32 tpm_add_cdrom_catalog(const u8 *addr, u32 length);
u32 tpm_option_rom(const void *addr, u32 len);
int tpm_is_working(void);
void tpm_menu(void);

#endif /* TCGBIOS_H */
