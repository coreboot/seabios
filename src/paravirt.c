// Paravirtualization support.
//
// Copyright (C) 2009 Red Hat Inc.
//
// Authors:
//  Gleb Natapov <gnatapov@redhat.com>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "config.h" // CONFIG_QEMU
#include "util.h" // dprintf
#include "byteorder.h" // be32_to_cpu
#include "ioport.h" // outw
#include "paravirt.h" // qemu_cfg_preinit
#include "smbios.h" // smbios_setup
#include "memmap.h" // add_e820
#include "cmos.h" // CMOS_*
#include "acpi.h" // acpi_setup
#include "mptable.h" // mptable_setup
#include "pci.h" // create_pirtable

struct e820_reservation {
    u64 address;
    u64 length;
    u32 type;
};

/* This CPUID returns the signature 'KVMKVMKVM' in ebx, ecx, and edx.  It
 * should be used to determine that a VM is running under KVM.
 */
#define KVM_CPUID_SIGNATURE     0x40000000

static void kvm_preinit(void)
{
    if (!CONFIG_QEMU)
        return;
    unsigned int eax, ebx, ecx, edx;
    char signature[13];

    cpuid(KVM_CPUID_SIGNATURE, &eax, &ebx, &ecx, &edx);
    memcpy(signature + 0, &ebx, 4);
    memcpy(signature + 4, &ecx, 4);
    memcpy(signature + 8, &edx, 4);
    signature[12] = 0;

    if (strcmp(signature, "KVMKVMKVM") == 0) {
        dprintf(1, "Running on KVM\n");
        PlatformRunningOn |= PF_KVM;
    }
}

void
qemu_ramsize_preinit(void)
{
    if (!CONFIG_QEMU)
        return;

    PlatformRunningOn = PF_QEMU;
    kvm_preinit();

    // On emulators, get memory size from nvram.
    u32 rs = ((inb_cmos(CMOS_MEM_EXTMEM2_LOW) << 16)
              | (inb_cmos(CMOS_MEM_EXTMEM2_HIGH) << 24));
    if (rs)
        rs += 16 * 1024 * 1024;
    else
        rs = (((inb_cmos(CMOS_MEM_EXTMEM_LOW) << 10)
               | (inb_cmos(CMOS_MEM_EXTMEM_HIGH) << 18))
              + 1 * 1024 * 1024);
    RamSize = rs;
    add_e820(0, rs, E820_RAM);

    // Check for memory over 4Gig
    u64 high = ((inb_cmos(CMOS_MEM_HIGHMEM_LOW) << 16)
                | ((u32)inb_cmos(CMOS_MEM_HIGHMEM_MID) << 24)
                | ((u64)inb_cmos(CMOS_MEM_HIGHMEM_HIGH) << 32));
    RamSizeOver4G = high;
    add_e820(0x100000000ull, high, E820_RAM);

    /* reserve 256KB BIOS area at the end of 4 GB */
    add_e820(0xfffc0000, 256*1024, E820_RESERVED);

    u32 count = qemu_cfg_e820_entries();
    if (count) {
        struct e820_reservation entry;
        int i;

        for (i = 0; i < count; i++) {
            qemu_cfg_e820_load_next(&entry);
            add_e820(entry.address, entry.length, entry.type);
        }
    } else if (runningOnKVM()) {
        // Backwards compatibility - provide hard coded range.
        // 4 pages before the bios, 3 pages for vmx tss pages, the
        // other page for EPT real mode pagetable
        add_e820(0xfffbc000, 4*4096, E820_RESERVED);
    }
}

void
qemu_biostable_setup(void)
{
    pirtable_setup();
    mptable_setup();
    smbios_setup();
    acpi_setup();
}


/****************************************************************
 * QEMU firmware config (fw_cfg) interface
 ****************************************************************/

int qemu_cfg_present;

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

static void
qemu_cfg_select(u16 f)
{
    outw(f, PORT_QEMU_CFG_CTL);
}

static void
qemu_cfg_read(void *buf, int len)
{
    insb(PORT_QEMU_CFG_DATA, buf, len);
}

static void
qemu_cfg_skip(int len)
{
    while (len--)
        inb(PORT_QEMU_CFG_DATA);
}

static void
qemu_cfg_read_entry(void *buf, int e, int len)
{
    qemu_cfg_select(e);
    qemu_cfg_read(buf, len);
}

void qemu_cfg_preinit(void)
{
    char *sig = "QEMU";
    int i;

    if (!CONFIG_QEMU)
        return;

    qemu_cfg_present = 1;

    qemu_cfg_select(QEMU_CFG_SIGNATURE);

    for (i = 0; i < 4; i++)
        if (inb(PORT_QEMU_CFG_DATA) != sig[i]) {
            qemu_cfg_present = 0;
            break;
        }
    dprintf(4, "qemu_cfg_present=%d\n", qemu_cfg_present);
}

u32 qemu_cfg_e820_entries(void)
{
    u32 cnt;

    if (!qemu_cfg_present)
        return 0;

    qemu_cfg_read_entry(&cnt, QEMU_CFG_E820_TABLE, sizeof(cnt));
    return cnt;
}

void* qemu_cfg_e820_load_next(void *addr)
{
    qemu_cfg_read(addr, sizeof(struct e820_reservation));
    return addr;
}

int qemu_cfg_get_numa_nodes(void)
{
    u64 cnt;

    qemu_cfg_read_entry(&cnt, QEMU_CFG_NUMA, sizeof(cnt));

    return (int)cnt;
}

void qemu_cfg_get_numa_data(u64 *data, int n)
{
    int i;

    for (i = 0; i < n; i++)
        qemu_cfg_read((u8*)(data + i), sizeof(u64));
}

static int
qemu_cfg_read_file(struct romfile_s *file, void *dst, u32 maxlen)
{
    if (file->size > maxlen)
        return -1;
    qemu_cfg_select(file->id);
    qemu_cfg_skip(file->rawsize);
    qemu_cfg_read(dst, file->size);
    return file->size;
}

static void
qemu_romfile_add(char *name, int select, int skip, int size)
{
    struct romfile_s *file = malloc_tmp(sizeof(*file));
    if (!file) {
        warn_noalloc();
        return;
    }
    memset(file, 0, sizeof(*file));
    strtcpy(file->name, name, sizeof(file->name));
    file->id = select;
    file->rawsize = skip; // Use rawsize to indicate skip length.
    file->size = size;
    file->copy = qemu_cfg_read_file;
    romfile_add(file);
}

#define SMBIOS_FIELD_ENTRY 0
#define SMBIOS_TABLE_ENTRY 1

struct qemu_smbios_header {
    u16 length;
    u8 headertype;
    u8 tabletype;
    u16 fieldoffset;
} PACKED;

// Populate romfile entries for legacy fw_cfg ports (that predate the
// "file" interface).
static void
qemu_cfg_legacy(void)
{
    // Misc config items.
    qemu_romfile_add("etc/show-boot-menu", QEMU_CFG_BOOT_MENU, 0, 2);
    qemu_romfile_add("etc/irq0-override", QEMU_CFG_IRQ0_OVERRIDE, 0, 1);
    qemu_romfile_add("etc/max-cpus", QEMU_CFG_MAX_CPUS, 0, 2);

    // ACPI tables
    char name[128];
    u16 cnt;
    qemu_cfg_read_entry(&cnt, QEMU_CFG_ACPI_TABLES, sizeof(cnt));
    int i, offset = sizeof(cnt);
    for (i = 0; i < cnt; i++) {
        u16 len;
        qemu_cfg_read(&len, sizeof(len));
        offset += sizeof(len);
        snprintf(name, sizeof(name), "acpi/table%d", i);
        qemu_romfile_add(name, QEMU_CFG_ACPI_TABLES, offset, len);
        qemu_cfg_skip(len);
        offset += len;
    }

    // SMBIOS info
    qemu_cfg_read_entry(&cnt, QEMU_CFG_SMBIOS_ENTRIES, sizeof(cnt));
    offset = sizeof(cnt);
    for (i = 0; i < cnt; i++) {
        struct qemu_smbios_header header;
        qemu_cfg_read(&header, sizeof(header));
        if (header.headertype == SMBIOS_FIELD_ENTRY) {
            snprintf(name, sizeof(name), "smbios/field%d-%d"
                     , header.tabletype, header.fieldoffset);
            qemu_romfile_add(name, QEMU_CFG_SMBIOS_ENTRIES
                             , offset + sizeof(header)
                             , header.length - sizeof(header));
        } else {
            snprintf(name, sizeof(name), "smbios/table%d-%d"
                     , header.tabletype, i);
            qemu_romfile_add(name, QEMU_CFG_SMBIOS_ENTRIES
                             , offset + 3, header.length - 3);
        }
        qemu_cfg_skip(header.length - sizeof(header));
        offset += header.length;
    }
}

struct QemuCfgFile {
    u32  size;        /* file size */
    u16  select;      /* write this to 0x510 to read it */
    u16  reserved;
    char name[56];
};

void qemu_romfile_init(void)
{
    if (!CONFIG_QEMU || !qemu_cfg_present)
        return;

    // Populate romfiles for legacy fw_cfg entries
    qemu_cfg_legacy();

    // Load files found in the fw_cfg file directory
    u32 count;
    qemu_cfg_read_entry(&count, QEMU_CFG_FILE_DIR, sizeof(count));
    count = be32_to_cpu(count);
    u32 e;
    for (e = 0; e < count; e++) {
        struct QemuCfgFile qfile;
        qemu_cfg_read(&qfile, sizeof(qfile));
        qemu_romfile_add(qfile.name, be16_to_cpu(qfile.select)
                         , 0, be32_to_cpu(qfile.size));
    }
}
