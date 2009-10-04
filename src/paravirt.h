#ifndef __PV_H
#define __PV_H

#include "util.h"

/* This CPUID returns the signature 'KVMKVMKVM' in ebx, ecx, and edx.  It
 * should be used to determine that a VM is running under KVM.
 */
#define KVM_CPUID_SIGNATURE     0x40000000

static inline int kvm_para_available(void)
{
    unsigned int eax, ebx, ecx, edx;
    char signature[13];

    cpuid(KVM_CPUID_SIGNATURE, &eax, &ebx, &ecx, &edx);
    memcpy(signature + 0, &ebx, 4);
    memcpy(signature + 4, &ecx, 4);
    memcpy(signature + 8, &edx, 4);
    signature[12] = 0;

    if (strcmp(signature, "KVMKVMKVM") == 0)
        return 1;

    return 0;
}

#define QEMU_CFG_SIGNATURE		0x00
#define QEMU_CFG_ID			0x01
#define QEMU_CFG_UUID			0x02
#define QEMU_CFG_NUMA			0x0d
#define QEMU_CFG_BOOT_MENU		0x0e
#define QEMU_CFG_MAX_CPUS		0x0f
#define QEMU_CFG_ARCH_LOCAL		0x8000
#define QEMU_CFG_ACPI_TABLES		(QEMU_CFG_ARCH_LOCAL + 0)
#define QEMU_CFG_SMBIOS_ENTRIES		(QEMU_CFG_ARCH_LOCAL + 1)

extern int qemu_cfg_present;

void qemu_cfg_port_probe(void);
int qemu_cfg_show_boot_menu(void);
void qemu_cfg_get_uuid(u8 *uuid);

#endif
