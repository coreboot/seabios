#ifndef __PV_H
#define __PV_H

#include "config.h" // CONFIG_COREBOOT
#include "util.h"

/* This CPUID returns the signature 'KVMKVMKVM' in ebx, ecx, and edx.  It
 * should be used to determine that a VM is running under KVM.
 */
#define KVM_CPUID_SIGNATURE     0x40000000

static inline int kvm_para_available(void)
{
    if (CONFIG_COREBOOT)
        return 0;
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

#define QEMU_CFG_SIGNATURE              0x00
#define QEMU_CFG_ID                     0x01
#define QEMU_CFG_UUID                   0x02
#define QEMU_CFG_NUMA                   0x0d
#define QEMU_CFG_BOOT_MENU              0x0e
#define QEMU_CFG_MAX_CPUS               0x0f
#define QEMU_CFG_FILE_DIR               0x19
#define QEMU_CFG_ARCH_LOCAL             0x8000
#define QEMU_CFG_ACPI_TABLES            (QEMU_CFG_ARCH_LOCAL + 0)
#define QEMU_CFG_SMBIOS_ENTRIES         (QEMU_CFG_ARCH_LOCAL + 1)
#define QEMU_CFG_IRQ0_OVERRIDE          (QEMU_CFG_ARCH_LOCAL + 2)
#define QEMU_CFG_E820_TABLE             (QEMU_CFG_ARCH_LOCAL + 3)

extern int qemu_cfg_present;

void qemu_cfg_port_probe(void);
int qemu_cfg_show_boot_menu(void);
void qemu_cfg_get_uuid(u8 *uuid);
int qemu_cfg_irq0_override(void);
u16 qemu_cfg_acpi_additional_tables(void);
u16 qemu_cfg_next_acpi_table_len(void);
void *qemu_cfg_next_acpi_table_load(void *addr, u16 len);
u16 qemu_cfg_smbios_entries(void);
size_t qemu_cfg_smbios_load_field(int type, size_t offset, void *addr);
int qemu_cfg_smbios_load_external(int type, char **p, unsigned *nr_structs,
                                  unsigned *max_struct_size, char *end);
int qemu_cfg_get_numa_nodes(void);
void qemu_cfg_get_numa_data(u64 *data, int n);
u16 qemu_cfg_get_max_cpus(void);
struct e820_reservation {
    u64 address;
    u64 length;
    u32 type;
};
u32 qemu_cfg_e820_entries(void);
void* qemu_cfg_e820_load_next(void *addr);
void qemu_cfg_romfile_setup(void);

#endif
