// System Management Mode support (on emulators)
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "pci.h" // pci_config_writel
#include "pci_regs.h" // PCI_DEVICE_ID
#include "util.h" // wbinvd
#include "config.h" // CONFIG_*
#include "ioport.h" // outb
#include "pci_ids.h" // PCI_VENDOR_ID_INTEL
#include "dev-q35.h"

ASM32FLAT(
    ".global smm_relocation_start\n"
    ".global smm_relocation_end\n"
    ".global smm_code_start\n"
    ".global smm_code_end\n"
    "  .code16\n"

    /* code to relocate SMBASE to 0xa0000 */
    "smm_relocation_start:\n"
    "  mov $" __stringify(BUILD_SMM_INIT_ADDR) " + 0x7efc, %ebx\n"
    "  addr32 mov (%ebx), %al\n"  /* revision ID to see if x86_64 or x86 */
    "  cmp $0x64, %al\n"
    "  je 1f\n"
    "  mov $" __stringify(BUILD_SMM_INIT_ADDR) " + 0x7ef8, %ebx\n"
    "  jmp 2f\n"
    "1:\n"
    "  mov $" __stringify(BUILD_SMM_INIT_ADDR) " + 0x7f00, %ebx\n"
    "2:\n"
    "  movl $" __stringify(BUILD_SMM_ADDR) " - 0x8000, %eax\n"
    "  addr32 movl %eax, (%ebx)\n"
    /* indicate to the BIOS that the SMM code was executed */
    "  mov $0x00, %al\n"
    "  movw $" __stringify(PORT_SMI_STATUS) ", %dx\n"
    "  outb %al, %dx\n"
    "  rsm\n"
    "smm_relocation_end:\n"

    /* minimal SMM code to enable or disable ACPI */
    "smm_code_start:\n"
    "  movw $" __stringify(PORT_SMI_CMD) ", %dx\n"
    "  inb %dx, %al\n"
    "  cmp $0xf0, %al\n"
    "  jne 1f\n"

    /* ACPI disable */
    "  mov $" __stringify(PORT_ACPI_PM_BASE) " + 0x04, %dx\n" /* PMCNTRL */
    "  inw %dx, %ax\n"
    "  andw $~1, %ax\n"
    "  outw %ax, %dx\n"

    "  jmp 2f\n"

    "1:\n"
    "  cmp $0xf1, %al\n"
    "  jne 2f\n"

    /* ACPI enable */
    "  mov $" __stringify(PORT_ACPI_PM_BASE) " + 0x04, %dx\n" /* PMCNTRL */
    "  inw %dx, %ax\n"
    "  orw $1, %ax\n"
    "  outw %ax, %dx\n"

    "2:\n"
    "  rsm\n"
    "smm_code_end:\n"
    "  .code32\n"
    );

extern u8 smm_relocation_start, smm_relocation_end;
extern u8 smm_code_start, smm_code_end;

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
    u32 value = inl(PORT_ACPI_PM_BASE + ICH9_PMIO_SMI_EN);
    if (value & ICH9_PMIO_SMI_EN_APMC_EN)
        return;

    /* enable the SMM memory window */
    pci_config_writeb(mch_bdf, Q35_HOST_BRIDGE_SMRAM, 0x02 | 0x48);

    smm_save_and_copy();

    /* enable SMI generation when writing to the APMC register */
    outl(value | ICH9_PMIO_SMI_EN_APMC_EN,
         PORT_ACPI_PM_BASE + ICH9_PMIO_SMI_EN);

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
