// System Management Mode support (on emulators)
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "pci.h" // PCIDevice
#include "util.h" // wbinvd

asm(
    ".global smm_relocation_start\n"
    ".global smm_relocation_end\n"
    ".global smm_code_start\n"
    ".global smm_code_end\n"
    "  .code16\n"

    /* code to relocate SMBASE to 0xa0000 */
    "smm_relocation_start:\n"
    "  mov $0x38000 + 0x7efc, %ebx\n"
    "  addr32 mov (%ebx), %al\n"  /* revision ID to see if x86_64 or x86 */
    "  cmp $0x64, %al\n"
    "  je 1f\n"
    "  mov $0x38000 + 0x7ef8, %ebx\n"
    "  jmp 2f\n"
    "1:\n"
    "  mov $0x38000 + 0x7f00, %ebx\n"
    "2:\n"
    "  movl $0xa0000, %eax\n"
    "  addr32 movl %eax, (%ebx)\n"
    /* indicate to the BIOS that the SMM code was executed */
    "  mov $0x00, %al\n"
    "  movw $0xb3, %dx\n"
    "  outb %al, %dx\n"
    "  rsm\n"
    "smm_relocation_end:\n"

    /* minimal SMM code to enable or disable ACPI */
    "smm_code_start:\n"
    "  movw $0xb2, %dx\n"
    "  inb %dx, %al\n"
    "  cmp $0xf0, %al\n"
    "  jne 1f\n"

    /* ACPI disable */
    "  mov $" __stringify(BUILD_PM_IO_BASE) " + 0x04, %dx\n" /* PMCNTRL */
    "  inw %dx, %ax\n"
    "  andw $~1, %ax\n"
    "  outw %ax, %dx\n"

    "  jmp 2f\n"

    "1:\n"
    "  cmp $0xf1, %al\n"
    "  jne 2f\n"

    /* ACPI enable */
    "  mov $" __stringify(BUILD_PM_IO_BASE) " + 0x04, %dx\n" /* PMCNTRL */
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

void
smm_init()
{
    if (!CONFIG_USE_SMM)
        return;

    // This code is hardcoded for PIIX4 Power Management device.
    PCIDevice i440_pcidev, d;
    int ret = pci_find_device(0x8086, 0x7113, 0, &d);
    if (ret)
        // Device not found
        return;
    ret = pci_find_device(0x8086, 0x1237, 0, &i440_pcidev);
    if (ret)
        return;

    /* check if SMM init is already done */
    u32 value = pci_config_readl(d, 0x58);
    if (value & (1 << 25))
        return;

    /* copy the SMM relocation code */
    memcpy((void *)0x38000, &smm_relocation_start,
           &smm_relocation_end - &smm_relocation_start);

    /* enable SMI generation when writing to the APMC register */
    pci_config_writel(d, 0x58, value | (1 << 25));

    /* init APM status port */
    outb(0x01, 0xb3);

    /* raise an SMI interrupt */
    outb(0x00, 0xb2);

    /* wait until SMM code executed */
    while (inb(0xb3) != 0x00)
        ;

    /* enable the SMM memory window */
    pci_config_writeb(i440_pcidev, 0x72, 0x02 | 0x48);

    /* copy the SMM code */
    memcpy((void *)0xa8000, &smm_code_start,
           &smm_code_end - &smm_code_start);
    wbinvd();

    /* close the SMM memory window and enable normal SMM */
    pci_config_writeb(i440_pcidev, 0x72, 0x02 | 0x08);
}
