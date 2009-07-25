// smbios table generation (on emulators)
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2006 Fabrice Bellard
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "memmap.h" // malloc_fseg
#include "biosvar.h" // GET_EBDA


/****************************************************************
 * UUID probe
 ****************************************************************/

#define QEMU_CFG_SIGNATURE  0x00
#define QEMU_CFG_ID         0x01
#define QEMU_CFG_UUID       0x02

static void
qemu_cfg_read(u8 *buf, u16 f, int len)
{
    outw(f, PORT_QEMU_CFG_CTL);
    while (len--)
        *(buf++) = inb(PORT_QEMU_CFG_DATA);
}

static int
qemu_cfg_port_probe()
{
    u8 sig[4] = "QEMU";
    u8 buf[4];
    qemu_cfg_read(buf, QEMU_CFG_SIGNATURE, 4);
    return *(u32*)buf == *(u32*)sig;
}

static void
uuid_probe(u8 *bios_uuid)
{
    // Default to UUID not set
    memset(bios_uuid, 0, 16);

    if (! CONFIG_UUID_BACKDOOR)
        return;
    if (CONFIG_COREBOOT)
        return;
    if (! qemu_cfg_port_probe())
        // Feature not available
        return;

    qemu_cfg_read(bios_uuid, QEMU_CFG_UUID, 16);
}


/****************************************************************
 * smbios tables
 ****************************************************************/

/* SMBIOS entry point -- must be written to a 16-bit aligned address
   between 0xf0000 and 0xfffff.
 */
struct smbios_entry_point {
	char anchor_string[4];
	u8 checksum;
	u8 length;
	u8 smbios_major_version;
	u8 smbios_minor_version;
	u16 max_structure_size;
	u8 entry_point_revision;
	u8 formatted_area[5];
	char intermediate_anchor_string[5];
	u8 intermediate_checksum;
	u16 structure_table_length;
	u32 structure_table_address;
	u16 number_of_structures;
	u8 smbios_bcd_revision;
} PACKED;

/* This goes at the beginning of every SMBIOS structure. */
struct smbios_structure_header {
	u8 type;
	u8 length;
	u16 handle;
} PACKED;

/* SMBIOS type 0 - BIOS Information */
struct smbios_type_0 {
	struct smbios_structure_header header;
	u8 vendor_str;
	u8 bios_version_str;
	u16 bios_starting_address_segment;
	u8 bios_release_date_str;
	u8 bios_rom_size;
	u8 bios_characteristics[8];
	u8 bios_characteristics_extension_bytes[2];
	u8 system_bios_major_release;
	u8 system_bios_minor_release;
	u8 embedded_controller_major_release;
	u8 embedded_controller_minor_release;
} PACKED;

/* SMBIOS type 1 - System Information */
struct smbios_type_1 {
	struct smbios_structure_header header;
	u8 manufacturer_str;
	u8 product_name_str;
	u8 version_str;
	u8 serial_number_str;
	u8 uuid[16];
	u8 wake_up_type;
	u8 sku_number_str;
	u8 family_str;
} PACKED;

/* SMBIOS type 3 - System Enclosure (v2.3) */
struct smbios_type_3 {
	struct smbios_structure_header header;
	u8 manufacturer_str;
	u8 type;
	u8 version_str;
	u8 serial_number_str;
	u8 asset_tag_number_str;
	u8 boot_up_state;
	u8 power_supply_state;
	u8 thermal_state;
	u8 security_status;
    u32 oem_defined;
    u8 height;
    u8 number_of_power_cords;
    u8 contained_element_count;
    // contained elements follow
} PACKED;

/* SMBIOS type 4 - Processor Information (v2.0) */
struct smbios_type_4 {
	struct smbios_structure_header header;
	u8 socket_designation_str;
	u8 processor_type;
	u8 processor_family;
	u8 processor_manufacturer_str;
	u32 processor_id[2];
	u8 processor_version_str;
	u8 voltage;
	u16 external_clock;
	u16 max_speed;
	u16 current_speed;
	u8 status;
	u8 processor_upgrade;
	u16 l1_cache_handle;
	u16 l2_cache_handle;
	u16 l3_cache_handle;
} PACKED;

/* SMBIOS type 16 - Physical Memory Array
 *   Associated with one type 17 (Memory Device).
 */
struct smbios_type_16 {
	struct smbios_structure_header header;
	u8 location;
	u8 use;
	u8 error_correction;
	u32 maximum_capacity;
	u16 memory_error_information_handle;
	u16 number_of_memory_devices;
} PACKED;

/* SMBIOS type 17 - Memory Device
 *   Associated with one type 19
 */
struct smbios_type_17 {
	struct smbios_structure_header header;
	u16 physical_memory_array_handle;
	u16 memory_error_information_handle;
	u16 total_width;
	u16 data_width;
	u16 size;
	u8 form_factor;
	u8 device_set;
	u8 device_locator_str;
	u8 bank_locator_str;
	u8 memory_type;
	u16 type_detail;
} PACKED;

/* SMBIOS type 19 - Memory Array Mapped Address */
struct smbios_type_19 {
	struct smbios_structure_header header;
	u32 starting_address;
	u32 ending_address;
	u16 memory_array_handle;
	u8 partition_width;
} PACKED;

/* SMBIOS type 20 - Memory Device Mapped Address */
struct smbios_type_20 {
	struct smbios_structure_header header;
	u32 starting_address;
	u32 ending_address;
	u16 memory_device_handle;
	u16 memory_array_mapped_address_handle;
	u8 partition_row_position;
	u8 interleave_position;
	u8 interleaved_data_depth;
} PACKED;

/* SMBIOS type 32 - System Boot Information */
struct smbios_type_32 {
	struct smbios_structure_header header;
	u8 reserved[6];
	u8 boot_status;
} PACKED;

/* SMBIOS type 127 -- End-of-table */
struct smbios_type_127 {
	struct smbios_structure_header header;
} PACKED;


/****************************************************************
 * smbios init
 ****************************************************************/

static void
smbios_entry_point_init(u16 max_structure_size,
                        u16 structure_table_length,
                        void *structure_table_address,
                        u16 number_of_structures)
{
    struct smbios_entry_point *ep = malloc_fseg(sizeof(*ep));
    if (! ep) {
        dprintf(1, "No space for smbios entry table!\n");
        return;
    }

    memcpy(ep->anchor_string, "_SM_", 4);
    ep->length = 0x1f;
    ep->smbios_major_version = 2;
    ep->smbios_minor_version = 4;
    ep->max_structure_size = max_structure_size;
    ep->entry_point_revision = 0;
    memset(ep->formatted_area, 0, 5);
    memcpy(ep->intermediate_anchor_string, "_DMI_", 5);

    ep->structure_table_length = structure_table_length;
    ep->structure_table_address = (u32)structure_table_address;
    ep->number_of_structures = number_of_structures;
    ep->smbios_bcd_revision = 0x24;

    ep->checksum -= checksum(ep, 0x10);

    ep->intermediate_checksum -= checksum((void*)ep + 0x10, ep->length - 0x10);

    dprintf(1, "SMBIOS ptr=%p table=%p\n", ep, structure_table_address);
}

/* Type 0 -- BIOS Information */
#define RELEASE_DATE_STR "01/01/2007"
static void *
smbios_type_0_init(void *start)
{
    struct smbios_type_0 *p = (struct smbios_type_0 *)start;

    p->header.type = 0;
    p->header.length = sizeof(struct smbios_type_0);
    p->header.handle = 0;

    p->vendor_str = 1;
    p->bios_version_str = 1;
    p->bios_starting_address_segment = 0xe800;
    p->bios_release_date_str = 2;
    p->bios_rom_size = 0; /* FIXME */

    memset(p->bios_characteristics, 0, sizeof(p->bios_characteristics));
    p->bios_characteristics[0] = 0x08; /* BIOS characteristics not supported */
    p->bios_characteristics_extension_bytes[0] = 0;
    p->bios_characteristics_extension_bytes[1] = 0;

    p->system_bios_major_release = 1;
    p->system_bios_minor_release = 0;
    p->embedded_controller_major_release = 0xff;
    p->embedded_controller_minor_release = 0xff;

    start += sizeof(struct smbios_type_0);
    memcpy((char *)start, CONFIG_APPNAME, sizeof(CONFIG_APPNAME));
    start += sizeof(CONFIG_APPNAME);
    memcpy((char *)start, RELEASE_DATE_STR, sizeof(RELEASE_DATE_STR));
    start += sizeof(RELEASE_DATE_STR);
    *((u8 *)start) = 0;

    return start+1;
}

/* Type 1 -- System Information */
static void *
smbios_type_1_init(void *start)
{
    struct smbios_type_1 *p = (struct smbios_type_1 *)start;
    p->header.type = 1;
    p->header.length = sizeof(struct smbios_type_1);
    p->header.handle = 0x100;

    p->manufacturer_str = 0;
    p->product_name_str = 0;
    p->version_str = 0;
    p->serial_number_str = 0;

    uuid_probe(p->uuid);

    p->wake_up_type = 0x06; /* power switch */
    p->sku_number_str = 0;
    p->family_str = 0;

    start += sizeof(struct smbios_type_1);
    *((u16 *)start) = 0;

    return start+2;
}

/* Type 3 -- System Enclosure */
static void *
smbios_type_3_init(void *start)
{
    struct smbios_type_3 *p = (struct smbios_type_3 *)start;

    p->header.type = 3;
    p->header.length = sizeof(struct smbios_type_3);
    p->header.handle = 0x300;

    p->manufacturer_str = 0;
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
    *((u16 *)start) = 0;

    return start+2;
}

/* Type 4 -- Processor Information */
static void *
smbios_type_4_init(void *start, unsigned int cpu_number)
{
    struct smbios_type_4 *p = (struct smbios_type_4 *)start;

    p->header.type = 4;
    p->header.length = sizeof(struct smbios_type_4);
    p->header.handle = 0x400 + cpu_number;

    p->socket_designation_str = 1;
    p->processor_type = 0x03; /* CPU */
    p->processor_family = 0x01; /* other */
    p->processor_manufacturer_str = 0;

    u32 cpuid_signature, ebx, ecx, cpuid_features;
    cpuid(1, &cpuid_signature, &ebx, &ecx, &cpuid_features);
    p->processor_id[0] = cpuid_signature;
    p->processor_id[1] = cpuid_features;

    p->processor_version_str = 0;
    p->voltage = 0;
    p->external_clock = 0;

    p->max_speed = 0; /* unknown */
    p->current_speed = 0; /* unknown */

    p->status = 0x41; /* socket populated, CPU enabled */
    p->processor_upgrade = 0x01; /* other */

    p->l1_cache_handle = 0xffff; /* cache information structure not provided */
    p->l2_cache_handle = 0xffff;
    p->l3_cache_handle = 0xffff;

    start += sizeof(struct smbios_type_4);

    memcpy((char *)start, "CPU  " "\0" "" "\0" "", 7);
	((char *)start)[4] = cpu_number + '0';

    return start+7;
}

/* Type 16 -- Physical Memory Array */
static void *
smbios_type_16_init(void *start)
{
    struct smbios_type_16 *p = (struct smbios_type_16*)start;

    p->header.type = 16;
    p->header.length = sizeof(struct smbios_type_16);
    p->header.handle = 0x1000;

    p->location = 0x01; /* other */
    p->use = 0x03; /* system memory */
    p->error_correction = 0x01; /* other */
    u64 memsize = RamSize + RamSizeOver4G;
    p->maximum_capacity = memsize / 1024;
    p->memory_error_information_handle = 0xfffe; /* none provided */
    p->number_of_memory_devices = 1;

    start += sizeof(struct smbios_type_16);
    *((u16 *)start) = 0;

    return start + 2;
}

/* Type 17 -- Memory Device */
static void *
smbios_type_17_init(void *start)
{
    struct smbios_type_17 *p = (struct smbios_type_17 *)start;

    p->header.type = 17;
    p->header.length = sizeof(struct smbios_type_17);
    p->header.handle = 0x1100;

    p->physical_memory_array_handle = 0x1000;
    p->total_width = 64;
    p->data_width = 64;
    /* truncate memory_size_mb to 16 bits and clear most significant
       bit [indicates size in MB] */
    u64 memsize = RamSize + RamSizeOver4G;
    p->size = (u16) (memsize / (1024*1024)) & 0x7fff;
    p->form_factor = 0x09; /* DIMM */
    p->device_set = 0;
    p->device_locator_str = 1;
    p->bank_locator_str = 0;
    p->memory_type = 0x07; /* RAM */
    p->type_detail = 0;

    start += sizeof(struct smbios_type_17);
    memcpy((char *)start, "DIMM 1", 7);
    start += 7;
    *((u8 *)start) = 0;

    return start+1;
}

/* Type 19 -- Memory Array Mapped Address */
static void *
smbios_type_19_init(void *start)
{
    struct smbios_type_19 *p = (struct smbios_type_19 *)start;

    p->header.type = 19;
    p->header.length = sizeof(struct smbios_type_19);
    p->header.handle = 0x1300;

    p->starting_address = 0;
    u64 memsize = RamSizeOver4G;
    if (memsize)
        memsize += 0x100000000ull;
    else
        memsize = RamSize;
    p->ending_address = memsize / 1024 - 1;
    p->memory_array_handle = 0x1000;
    p->partition_width = 1;

    start += sizeof(struct smbios_type_19);
    *((u16 *)start) = 0;

    return start + 2;
}

/* Type 20 -- Memory Device Mapped Address */
static void *
smbios_type_20_init(void *start)
{
    struct smbios_type_20 *p = (struct smbios_type_20 *)start;

    p->header.type = 20;
    p->header.length = sizeof(struct smbios_type_20);
    p->header.handle = 0x1400;

    p->starting_address = 0;
    u64 memsize = RamSizeOver4G;
    if (memsize)
        memsize += 0x100000000ull;
    else
        memsize = RamSize;
    p->ending_address = memsize / 1024 - 1;
    p->memory_device_handle = 0x1100;
    p->memory_array_mapped_address_handle = 0x1300;
    p->partition_row_position = 1;
    p->interleave_position = 0;
    p->interleaved_data_depth = 0;

    start += sizeof(struct smbios_type_20);

    *((u16 *)start) = 0;
    return start+2;
}

/* Type 32 -- System Boot Information */
static void *
smbios_type_32_init(void *start)
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
smbios_type_127_init(void *start)
{
    struct smbios_type_127 *p = (struct smbios_type_127 *)start;

    p->header.type = 127;
    p->header.length = sizeof(struct smbios_type_127);
    p->header.handle = 0x7f00;

    start += sizeof(struct smbios_type_127);
    *((u16 *)start) = 0;

    return start + 2;
}

void
smbios_init(void)
{
    if (! CONFIG_SMBIOS)
        return;

    dprintf(3, "init SMBIOS tables\n");

    char *start = malloc_high(2048); // XXX - determine real size
    if (! start) {
        dprintf(1, "No memory for smbios tables\n");
        return;
    }

    u32 nr_structs = 0, max_struct_size = 0;
    char *q, *p = start;

#define add_struct(fn) { \
    q = (fn); \
    nr_structs++; \
    if ((q - p) > max_struct_size) \
        max_struct_size = q - p; \
    p = q; \
}

    add_struct(smbios_type_0_init(p));
    add_struct(smbios_type_1_init(p));
    add_struct(smbios_type_3_init(p));
    int cpu_num, smp_cpus = CountCPUs;
    for (cpu_num = 1; cpu_num <= smp_cpus; cpu_num++)
        add_struct(smbios_type_4_init(p, cpu_num));
    add_struct(smbios_type_16_init(p));
    add_struct(smbios_type_17_init(p));
    add_struct(smbios_type_19_init(p));
    add_struct(smbios_type_20_init(p));
    add_struct(smbios_type_32_init(p));
    add_struct(smbios_type_127_init(p));

#undef add_struct

    smbios_entry_point_init(max_struct_size, p - start, start, nr_structs);
}
