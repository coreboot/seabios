// System Management Mode support (on emulators)
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "config.h" // CONFIG_*
#include "dev-q35.h"
#include "hw/pci.h" // pci_config_writel
#include "hw/pci_ids.h" // PCI_VENDOR_ID_INTEL
#include "hw/pci_regs.h" // PCI_DEVICE_ID
#include "output.h" // dprintf
#include "paravirt.h" // PORT_SMI_STATUS
#include "string.h" // memcpy
#include "util.h" // smm_setup
#include "x86.h" // wbinvd

extern u8 smm_relocation_start, smm_relocation_end;
ASM32FLAT(
    ".global smm_relocation_start, smm_relocation_end\n"
    "  .code16gcc\n"

    /* code to relocate SMBASE to 0xa0000 */
    "smm_relocation_start:\n"
    "  movl $" __stringify(BUILD_SMM_INIT_ADDR) " + 0x7efc, %ebx\n"
    "  addr32 movb (%ebx), %al\n"  /* revision ID to see if x86_64 or x86 */
    "  cmpb $0x64, %al\n"
    "  je 1f\n"
    "  movl $" __stringify(BUILD_SMM_INIT_ADDR) " + 0x7ef8, %ebx\n"
    "  jmp 2f\n"
    "1:\n"
    "  movl $" __stringify(BUILD_SMM_INIT_ADDR) " + 0x7f00, %ebx\n"
    "2:\n"
    "  movl $" __stringify(BUILD_SMM_ADDR) " - 0x8000, %eax\n"
    "  addr32 movl %eax, (%ebx)\n"
    /* indicate to the BIOS that the SMM code was executed */
    "  movb $0x00, %al\n"
    "  movw $" __stringify(PORT_SMI_STATUS) ", %dx\n"
    "  outb %al, %dx\n"
    "  rsm\n"
    "smm_relocation_end:\n"
    "  .code32\n"
    );

extern u8 smm_code_start, smm_code_end;
ASM32FLAT(
    ".global smm_code_start, smm_code_end\n"
    "  .code16gcc\n"
    "smm_code_start:\n"
    "  rsm\n"
    "smm_code_end:\n"
    "  .code32\n"
    );

static void
smm_save_and_copy(void)
{
    /* save original memory content */
    memcpy((void *)BUILD_SMM_ADDR, (void *)BUILD_SMM_INIT_ADDR, BUILD_SMM_SIZE);

    /* copy the SMM relocation code */
    memcpy((void *)BUILD_SMM_INIT_ADDR, &smm_relocation_start,
           &smm_relocation_end - &smm_relocation_start);
}

static void
smm_relocate_and_restore(void)
{
    /* init APM status port */
    outb(0x01, PORT_SMI_STATUS);

    /* raise an SMI interrupt */
    outb(0x00, PORT_SMI_CMD);

    /* wait until SMM code executed */
    while (inb(PORT_SMI_STATUS) != 0x00)
        ;

    /* restore original memory content */
    memcpy((void *)BUILD_SMM_INIT_ADDR, (void *)BUILD_SMM_ADDR, BUILD_SMM_SIZE);

    /* copy the SMM code */
    memcpy((void *)BUILD_SMM_ADDR, &smm_code_start
           , &smm_code_end - &smm_code_start);
    wbinvd();
}

#define I440FX_SMRAM    0x72
#define PIIX_DEVACTB    0x58
#define PIIX_APMC_EN    (1 << 25)

// This code is hardcoded for PIIX4 Power Management device.
static void piix4_apmc_smm_setup(int isabdf, int i440_bdf)
{
    /* check if SMM init is already done */
    u32 value = pci_config_readl(isabdf, PIIX_DEVACTB);
    if (value & PIIX_APMC_EN)
        return;

    /* enable the SMM memory window */
    pci_config_writeb(i440_bdf, I440FX_SMRAM, 0x02 | 0x48);

    smm_save_and_copy();

    /* enable SMI generation when writing to the APMC register */
    pci_config_writel(isabdf, PIIX_DEVACTB, value | PIIX_APMC_EN);

    smm_relocate_and_restore();

    /* close the SMM memory window and enable normal SMM */
    pci_config_writeb(i440_bdf, I440FX_SMRAM, 0x02 | 0x08);
}

/* PCI_VENDOR_ID_INTEL && PCI_DEVICE_ID_INTEL_ICH9_LPC */
void ich9_lpc_apmc_smm_setup(int isabdf, int mch_bdf)
{
    /* check if SMM init is already done */
    u32 value = inl(acpi_pm_base + ICH9_PMIO_SMI_EN);
    if (value & ICH9_PMIO_SMI_EN_APMC_EN)
        return;

    /* enable the SMM memory window */
    pci_config_writeb(mch_bdf, Q35_HOST_BRIDGE_SMRAM, 0x02 | 0x48);

    smm_save_and_copy();

    /* enable SMI generation when writing to the APMC register */
    outl(value | ICH9_PMIO_SMI_EN_APMC_EN,
         acpi_pm_base + ICH9_PMIO_SMI_EN);

    smm_relocate_and_restore();

    /* close the SMM memory window and enable normal SMM */
    pci_config_writeb(mch_bdf, Q35_HOST_BRIDGE_SMRAM, 0x02 | 0x08);
}

static int SMMISADeviceBDF = -1, SMMPMDeviceBDF = -1;

void
smm_device_setup(void)
{
    if (!CONFIG_USE_SMM)
        return;

    struct pci_device *isapci, *pmpci;
    isapci = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_3);
    pmpci = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82441);
    if (isapci && pmpci) {
        SMMISADeviceBDF = isapci->bdf;
        SMMPMDeviceBDF = pmpci->bdf;
        return;
    }
    isapci = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH9_LPC);
    pmpci = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_Q35_MCH);
    if (isapci && pmpci) {
        SMMISADeviceBDF = isapci->bdf;
        SMMPMDeviceBDF = pmpci->bdf;
    }
}

void
smm_setup(void)
{
    if (!CONFIG_USE_SMM || SMMISADeviceBDF < 0)
        return;

    dprintf(3, "init smm\n");
    u16 device = pci_config_readw(SMMISADeviceBDF, PCI_DEVICE_ID);
    if (device == PCI_DEVICE_ID_INTEL_82371AB_3)
        piix4_apmc_smm_setup(SMMISADeviceBDF, SMMPMDeviceBDF);
    else
        ich9_lpc_apmc_smm_setup(SMMISADeviceBDF, SMMPMDeviceBDF);
}
