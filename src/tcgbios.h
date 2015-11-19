#ifndef TCGBIOS_H
#define TCGBIOS_H

#include "types.h"

#define STATUS_FLAG_SHUTDOWN        (1 << 0)

struct iovec
{
    size_t length;
    const void *data;
};

enum ipltype {
    IPL_BCV = 0,
    IPL_EL_TORITO_1,
    IPL_EL_TORITO_2
};

struct bregs;
void tpm_interrupt_handler32(struct bregs *regs);

void tpm_setup(void);
void tpm_prepboot(void);
void tpm_s3_resume(void);
u32 tpm_add_bcv(u32 bootdrv, const u8 *addr, u32 length);
u32 tpm_add_cdrom(u32 bootdrv, const u8 *addr, u32 length);
u32 tpm_add_cdrom_catalog(const u8 *addr, u32 length);
u32 tpm_option_rom(const void *addr, u32 len);

#endif /* TCGBIOS_H */
