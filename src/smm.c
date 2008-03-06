/*
 * smm.c
 * SMM support for BIOS.
 * 
 * Copyright (C) 2002  MandrakeSoft S.A.
 * Copyright (C) 2008  Nguyen Anh Quynh <aquynh@gmail.com>
 * 
 * This file may be distributed under the terms of the GNU GPLv3 license.
 */

#include "types.h"
#include "pci.h"
#include "smm.h" // smm_init
#include "config.h" // CONFIG_*
#include "util.h" // memcpy

#ifdef CONFIG_SMM

asm (
"    .global smm_relocation_start                               \n"
"    .global smm_relocation_end                                 \n"
"    .global smm_code_start                                     \n"
"    .global smm_code_end                                       \n"
"                                                               \n"
"    .code16                                                    \n"
"    /* code to relocate SMBASE to 0xa0000 */                   \n"
"smm_relocation_start:                                          \n"
"    movl $0x38000 + 0x7efc, %ebx                               \n"
"    /* revision ID to see if x86_64 or x86 */                  \n"
"    mov (%ebx), %al                                            \n"
"    cmpb $0x64, %al                                            \n"
"    je 1f                                                      \n"
"    movl $0x38000 + 0x7ef8, %ebx                               \n"
"    jmp 2f                                                     \n"
"1:                                                             \n"
"    movl $0x38000 + 0x7f00, %ebx                               \n"
"2:                                                             \n"
"    movl $0xa0000, %eax                                        \n"
"    movl %eax, (%ebx)                                          \n"
"    /* indicate to the BIOS that the SMM code was executed */  \n"
"    mov $0x00, %al                                             \n"
"    movw $0xb3, %dx                                            \n"
"    outb %al, %dx                                              \n"
"    rsm                                                        \n"
"smm_relocation_end:                                            \n"
"                                                               \n"
"    /* minimal SMM code to enable or disable ACPI */           \n"
"smm_code_start:                                                \n"
"    movw $0xb2, %dx                                            \n"
"    inb %dx, %al                                               \n"
"    cmp $0xf0, %al                                             \n"
"    jne 1f                                                     \n"
"                                                               \n"
"    /* ACPI disable */                                         \n"
"    //mov $PM_IO_BASE + 0x04, %dx /* PMCNTRL */                \n"
"    mov $0xb000 + 0x04, %dx /* PMCNTRL */                      \n"
"    inw %dx, %ax                                               \n"
"    andw $~1, %ax                                              \n"
"    outw %ax, %dx                                              \n"
"                                                               \n"
"    jmp 2f                                                     \n"
"1:                                                             \n"
"    cmp $0xf1, %al                                             \n"
"    jne 2f                                                     \n"
"                                                               \n"
"    /* ACPI enable */                                          \n"
"    //mov $PM_IO_BASE + 0x04, %dx /* PMCNTRL */                \n"
"    mov $0xb000 + 0x04, %dx /* PMCNTRL */                      \n"
"    inw %dx, %ax                                               \n"
"    orw $1, %ax                                                \n"
"    outw %ax, %dx                                              \n"
"                                                               \n"
"2:                                                             \n"
"    rsm                                                        \n"
"smm_code_end:                                                  \n"
"    .code32                                                    \n"
    );

extern u8 smm_relocation_start, smm_relocation_end;
extern u8 smm_code_start, smm_code_end;

void
smm_init(PCIDevice *d)
{
    u32 value;

    /* check if SMM init is already done */
    value = pci_config_readl(d, 0x58);
    if ((value & (1 << 25)) == 0) {
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
        while (inb(0xb3) != 0x00);

        /* enable the SMM memory window */
        pci_config_writeb(&i440_pcidev, 0x72, 0x02 | 0x48);

        /* copy the SMM code */
        memcpy((void *)0xa8000, &smm_code_start,
               &smm_code_end - &smm_code_start);
        wbinvd();

        /* close the SMM memory window and enable normal SMM */
        pci_config_writeb(&i440_pcidev, 0x72, 0x02 | 0x08);
    }
}
#endif
