// smbios table generation (on emulators)
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "biosvar.h" // GET_EBDA
#include "paravirt.h" // qemu_cfg_smbios_load_field
#include "smbios.h" // struct smbios_entry_point

static void
smbios_entry_point_init(u16 max_structure_size,
                        u16 structure_table_length,
                        void *structure_table_address,
                        u16 number_of_structures)
{
    struct smbios_entry_point *ep = malloc_fseg(sizeof(*ep));
    void *finaltable = malloc_high(structure_table_length);
    if (!ep || !finaltable) {
        dprintf(1, "No space for smbios tables!\n");
        free(ep);
        free(finaltable);
        return;
    }
    memcpy(finaltable, structure_table_address, structure_table_length);

    memcpy(ep->anchor_string, "_SM_", 4);
    ep->length = 0x1f;
    ep->smbios_major_version = 2;
    ep->smbios_minor_version = 4;
    ep->max_structure_size = max_structure_size;
    ep->entry_point_revision = 0;
    memset(ep->formatted_area, 0, 5);
    memcpy(ep->intermediate_anchor_string, "_DMI_", 5);

    ep->structure_table_length = structure_table_length;
    ep->structure_table_address = (u32)finaltable;
    ep->number_of_structures = number_of_structures;
    ep->smbios_bcd_revision = 0x24;

    ep->checksum -= checksum(ep, 0x10);

    ep->intermediate_checksum -= checksum((void*)ep + 0x10, ep->length - 0x10);

    dprintf(1, "SMBIOS ptr=%p table=%p\n", ep, finaltable);
}

#define load_str_field_with_default(type, field, def)                   \
    do {                                                                \
        size = qemu_cfg_smbios_load_field(type,                         \
                                 offsetof(struct smbios_type_##type,    \
                                          field), end);                 \
        if (size > 0) {                                                 \
            end += size;                                                \
        } else {                                                        \
            memcpy(end, def, sizeof(def));                              \
            end += sizeof(def);                                         \
        }                                                               \
        p->field = ++str_index;                                         \
    } while (0)

#define load_str_field_or_skip(type, field) \
    do {                                                                \
        size = qemu_cfg_smbios_load_field(type,                         \
                                 offsetof(struct smbios_type_##type,    \
                                          field), end);                 \
        if (size > 0) {                                                 \
            end += size;                                                \
            p->field = ++str_index;                                     \
        } else {                                                        \
            p->field = 0;                                               \
        }                                                               \
    } while (0)

/* Type 0 -- BIOS Information */
#define RELEASE_DATE_STR "01/01/2007"
static void *
smbios_init_type_0(void *start)
{
    struct smbios_type_0 *p = (struct smbios_type_0 *)start;
    char *end = (char *)start + sizeof(struct smbios_type_0);
    size_t size;
    int str_index = 0;

    p->header.type = 0;
    p->header.length = sizeof(struct smbios_type_0);
    p->header.handle = 0;

    load_str_field_with_default(0, vendor_str, CONFIG_APPNAME);
    load_str_field_with_default(0, bios_version_str, CONFIG_APPNAME);

    p->bios_starting_address_segment = 0xe800;

    load_str_field_with_default(0, bios_release_date_str, RELEASE_DATE_STR);

    p->bios_rom_size = 0; /* FIXME */

    memset(p->bios_characteristics, 0, 8);
    p->bios_characteristics[0] = 0x08; /* BIOS characteristics not supported */
    p->bios_characteristics_extension_bytes[0] = 0;
    /* Enable targeted content distribution. Needed for SVVP */
    p->bios_characteristics_extension_bytes[1] = 4;

    if (!qemu_cfg_smbios_load_field(0, offsetof(struct smbios_type_0,
                                                system_bios_major_release),
                                    &p->system_bios_major_release))
        p->system_bios_major_release = 1;

    if (!qemu_cfg_smbios_load_field(0, offsetof(struct smbios_type_0,
                                                system_bios_minor_release),
                                    &p->system_bios_minor_release))
        p->system_bios_minor_release = 0;

    p->embedded_controller_major_release = 0xff;
    p->embedded_controller_minor_release = 0xff;

    *end = 0;
    end++;

    return end;
}

/* Type 1 -- System Information */
static void *
smbios_init_type_1(void *start)
{
    struct smbios_type_1 *p = (struct smbios_type_1 *)start;
    char *end = (char *)start + sizeof(struct smbios_type_1);
    size_t size;
    int str_index = 0;

    p->header.type = 1;
    p->header.length = sizeof(struct smbios_type_1);
    p->header.handle = 0x100;

    load_str_field_with_default(1, manufacturer_str, CONFIG_APPNAME);
    load_str_field_with_default(1, product_name_str, CONFIG_APPNAME);
    load_str_field_or_skip(1, version_str);
    load_str_field_or_skip(1, serial_number_str);

    size = qemu_cfg_smbios_load_field(1, offsetof(struct smbios_type_1,
                                                  uuid), &p->uuid);
    if (size == 0)
        memset(p->uuid, 0, 16);

    p->wake_up_type = 0x06; /* power switch */

    load_str_field_or_skip(1, sku_number_str);
    load_str_field_or_skip(1, family_str);

    *end = 0;
    end++;
    if (!str_index) {
        *end = 0;
        end++;
    }

    return end;
}

/* Type 3 -- System Enclosure */
static void *
smbios_init_type_3(void *start)
{
    struct smbios_type_3 *p = (struct smbios_type_3 *)start;

    p->header.type = 3;
    p->header.length = sizeof(struct smbios_type_3);
    p->header.handle = 0x300;

    p->manufacturer_str = 1;
    p->type = 0x01; /* other */
    p->version_str = 0;
    p->serial_number_str = 0;
    p->asset_tag_number_str = 0;
    p->boot_up_state = 0x03; /* safe */
    p->power_supply_state = 0x03; /* safe */
    p->thermal_state = 0x03; /* safe */
    p->security_status = 0x02; /* unknown */
    p->oem_defined = 0;
    p->height = 0;
    p->number_of_power_cords = 0;
    p->contained_element_count = 0;

    start += sizeof(struct smbios_type_3);
    memcpy((char *)start, CONFIG_APPNAME"\0\0", sizeof(CONFIG_APPNAME) + 1);

    return start + sizeof(CONFIG_APPNAME) + 1;
}

/* Type 4 -- Processor Information */
static void *
smbios_init_type_4(void *start, unsigned int cpu_number)
{
    struct smbios_type_4 *p = (struct smbios_type_4 *)start;

    p->header.type = 4;
    p->header.length = sizeof(struct smbios_type_4);
    p->header.handle = 0x400 + cpu_number;

    p->socket_designation_str = 1;
    p->processor_type = 0x03; /* CPU */
    p->processor_family = 0x01; /* other */
    p->processor_manufacturer_str = 2;

    u32 cpuid_signature, ebx, ecx, cpuid_features;
    cpuid(1, &cpuid_signature, &ebx, &ecx, &cpuid_features);
    p->processor_id[0] = cpuid_signature;
    p->processor_id[1] = cpuid_features;

    p->processor_version_str = 0;
    p->voltage = 0;
    p->external_clock = 0;

    p->max_speed = 2000;
    p->current_speed = 2000;

    p->status = 0x41; /* socket populated, CPU enabled */
    p->processor_upgrade = 0x01; /* other */

    p->l1_cache_handle = 0xffff; /* cache information structure not provided */
    p->l2_cache_handle = 0xffff;
    p->l3_cache_handle = 0xffff;

    start += sizeof(struct smbios_type_4);

    snprintf((char*)start, 6, "CPU%2x", cpu_number);
    start += 6;
    memcpy((char *)start, CONFIG_APPNAME"\0\0", sizeof(CONFIG_APPNAME) + 1);

    return start + sizeof(CONFIG_APPNAME) + 1;
}

/* Type 16 -- Physical Memory Array */
static void *
smbios_init_type_16(void *start, u32 memory_size_mb, int nr_mem_devs)
{
    struct smbios_type_16 *p = (struct smbios_type_16*)start;

    p->header.type = 16;
    p->header.length = sizeof(struct smbios_type_16);
    p->header.handle = 0x1000;

    p->location = 0x01; /* other */
    p->use = 0x03; /* system memory */
    p->error_correction = 0x06; /* Multi-bit ECC to make Microsoft happy */
    p->maximum_capacity = memory_size_mb * 1024;
    p->memory_error_information_handle = 0xfffe; /* none provided */
    p->number_of_memory_devices = nr_mem_devs;

    start += sizeof(struct smbios_type_16);
    *((u16 *)start) = 0;

    return start + 2;
}

/* Type 17 -- Memory Device */
static void *
smbios_init_type_17(void *start, u32 memory_size_mb, int instance)
{
    struct smbios_type_17 *p = (struct smbios_type_17 *)start;

    p->header.type = 17;
    p->header.length = sizeof(struct smbios_type_17);
    p->header.handle = 0x1100 + instance;

    p->physical_memory_array_handle = 0x1000;
    p->total_width = 64;
    p->data_width = 64;
/* TODO: should assert in case something is wrong   ASSERT((memory_size_mb & ~0x7fff) == 0); */
    p->size = memory_size_mb;
    p->form_factor = 0x09; /* DIMM */
    p->device_set = 0;
    p->device_locator_str = 1;
    p->bank_locator_str = 0;
    p->memory_type = 0x07; /* RAM */
    p->type_detail = 0;

    start += sizeof(struct smbios_type_17);
    memcpy((char *)start, "DIMM 0", 7);
    ((char*)start)[5] += instance;
    start += 7;
    *((u8 *)start) = 0;

    return start+1;
}

/* Type 19 -- Memory Array Mapped Address */
static void *
smbios_init_type_19(void *start, u32 memory_size_mb, int instance)
{
    struct smbios_type_19 *p = (struct smbios_type_19 *)start;

    p->header.type = 19;
    p->header.length = sizeof(struct smbios_type_19);
    p->header.handle = 0x1300 + instance;

    p->starting_address = instance << 24;
    p->ending_address = p->starting_address + (memory_size_mb << 10) - 1;
    p->memory_array_handle = 0x1000;
    p->partition_width = 1;

    start += sizeof(struct smbios_type_19);
    *((u16 *)start) = 0;

    return start + 2;
}

/* Type 20 -- Memory Device Mapped Address */
static void *
smbios_init_type_20(void *start, u32 memory_size_mb, int instance)
{
    struct smbios_type_20 *p = (struct smbios_type_20 *)start;

    p->header.type = 20;
    p->header.length = sizeof(struct smbios_type_20);
    p->header.handle = 0x1400 + instance;

    p->starting_address = instance << 24;
    p->ending_address = p->starting_address + (memory_size_mb << 10) - 1;
    p->memory_device_handle = 0x1100 + instance;
    p->memory_array_mapped_address_handle = 0x1300 + instance;
    p->partition_row_position = 1;
    p->interleave_position = 0;
    p->interleaved_data_depth = 0;

    start += sizeof(struct smbios_type_20);

    *((u16 *)start) = 0;
    return start+2;
}

/* Type 32 -- System Boot Information */
static void *
smbios_init_type_32(void *start)
{
    struct smbios_type_32 *p = (struct smbios_type_32 *)start;

    p->header.type = 32;
    p->header.length = sizeof(struct smbios_type_32);
    p->header.handle = 0x2000;
    memset(p->reserved, 0, 6);
    p->boot_status = 0; /* no errors detected */

    start += sizeof(struct smbios_type_32);
    *((u16 *)start) = 0;

    return start+2;
}

/* Type 127 -- End of Table */
static void *
smbios_init_type_127(void *start)
{
    struct smbios_type_127 *p = (struct smbios_type_127 *)start;

    p->header.type = 127;
    p->header.length = sizeof(struct smbios_type_127);
    p->header.handle = 0x7f00;

    start += sizeof(struct smbios_type_127);
    *((u16 *)start) = 0;

    return start + 2;
}

#define TEMPSMBIOSSIZE (32 * 1024)

void
smbios_init(void)
{
    if (! CONFIG_SMBIOS)
        return;

    dprintf(3, "init SMBIOS tables\n");

    char *start = malloc_tmphigh(TEMPSMBIOSSIZE);
    if (! start) {
        dprintf(1, "No memory for temp smbios table\n");
        return;
    }

    u32 nr_structs = 0, max_struct_size = 0;
    char *q, *p = start;
    char *end = start + TEMPSMBIOSSIZE - sizeof(struct smbios_type_127);

#define add_struct(type, args...)                                       \
    do {                                                                \
        if (!qemu_cfg_smbios_load_external(type, &p, &nr_structs,       \
                                           &max_struct_size, end)) {    \
            q = smbios_init_type_##type(args);                          \
            nr_structs++;                                               \
            if ((q - p) > max_struct_size)                              \
                max_struct_size = q - p;                                \
            p = q;                                                      \
        }                                                               \
    } while (0)

    add_struct(0, p);
    add_struct(1, p);
    add_struct(3, p);

    int cpu_num;
    for (cpu_num = 1; cpu_num <= MaxCountCPUs; cpu_num++)
        add_struct(4, p, cpu_num);
    u64 memsize = RamSizeOver4G;
    if (memsize)
        memsize += 0x100000000ull;
    else
        memsize = RamSize;
    memsize = memsize / (1024 * 1024);
    int nr_mem_devs = (memsize + 0x3fff) >> 14;
    add_struct(16, p, memsize, nr_mem_devs);
    int i;
    for (i = 0; i < nr_mem_devs; i++) {
        u32 dev_memsize = ((i == (nr_mem_devs - 1))
                           ? (((memsize-1) & 0x3fff)+1) : 0x4000);
        add_struct(17, p, dev_memsize, i);
        add_struct(19, p, dev_memsize, i);
        add_struct(20, p, dev_memsize, i);
    }

    add_struct(32, p);
    /* Add any remaining provided entries before the end marker */
    for (i = 0; i < 256; i++)
        qemu_cfg_smbios_load_external(i, &p, &nr_structs, &max_struct_size,
                                      end);
    add_struct(127, p);

#undef add_struct

    smbios_entry_point_init(max_struct_size, p - start, start, nr_structs);
    free(start);
}
