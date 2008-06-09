#ifndef __E820MAP_H
#define __E820MAP_H

#include "types.h" // u64

#define E820_RAM          1
#define E820_RESERVED     2
#define E820_ACPI         3
#define E820_NVS          4
#define E820_UNUSABLE     5

struct e820entry {
    u64 start;
    u64 size;
    u32 type;
};

void add_e820(u64 start, u64 size, u32 type);
void memmap_setup();
void memmap_finalize();

// Space for exported bios tables.
extern u32 bios_table_cur_addr, bios_table_end_addr;

#endif // e820map.h
