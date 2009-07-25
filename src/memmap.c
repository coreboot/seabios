// Support for building memory maps suitable for int 15 e820 calls.
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "memmap.h" // struct e820entry
#include "util.h" // dprintf.h
#include "biosvar.h" // SET_EBDA


/****************************************************************
 * e820 memory map
 ****************************************************************/

// Remove an entry from the e820_list.
static void
remove_e820(int i)
{
    e820_count--;
    memmove(&e820_list[i], &e820_list[i+1]
            , sizeof(e820_list[0]) * (e820_count - i));
}

// Insert an entry in the e820_list at the given position.
static void
insert_e820(int i, u64 start, u64 size, u32 type)
{
    if (e820_count >= CONFIG_MAX_E820) {
        dprintf(1, "Overflowed e820 list!\n");
        return;
    }

    memmove(&e820_list[i+1], &e820_list[i]
            , sizeof(e820_list[0]) * (e820_count - i));
    e820_count++;
    struct e820entry *e = &e820_list[i];
    e->start = start;
    e->size = size;
    e->type = type;
}

// Show the current e820_list.
static void
dump_map()
{
    dprintf(1, "e820 map has %d items:\n", e820_count);
    int i;
    for (i=0; i<e820_count; i++) {
        struct e820entry *e = &e820_list[i];
        u64 e_end = e->start + e->size;
        dprintf(1, "  %d: %08x%08x - %08x%08x = %d\n", i
                , (u32)(e->start >> 32), (u32)e->start
                , (u32)(e_end >> 32), (u32)e_end
                , e->type);
    }
}

// Add a new entry to the list.  This scans for overlaps and keeps the
// list sorted.
void
add_e820(u64 start, u64 size, u32 type)
{
    dprintf(8, "Add to e820 map: %08x %08x %d\n", (u32)start, (u32)size, type);

    if (! size)
        // Huh?  Nothing to do.
        return;

    // Find position of new item (splitting existing item if needed).
    u64 end = start + size;
    int i;
    for (i=0; i<e820_count; i++) {
        struct e820entry *e = &e820_list[i];
        u64 e_end = e->start + e->size;
        if (start > e_end)
            continue;
        // Found position - check if an existing item needs to be split.
        if (start > e->start) {
            if (type == e->type) {
                // Same type - merge them.
                size += start - e->start;
                start = e->start;
            } else {
                // Split existing item.
                e->size = start - e->start;
                i++;
                if (e_end > end)
                    insert_e820(i, end, e_end - end, e->type);
            }
        }
        break;
    }
    // Remove/adjust existing items that are overlapping.
    while (i<e820_count) {
        struct e820entry *e = &e820_list[i];
        if (end < e->start)
            // No overlap - done.
            break;
        u64 e_end = e->start + e->size;
        if (end >= e_end) {
            // Existing item completely overlapped - remove it.
            remove_e820(i);
            continue;
        }
        // Not completely overlapped - adjust its start.
        e->start = end;
        e->size = e_end - end;
        if (type == e->type) {
            // Same type - merge them.
            size += e->size;
            remove_e820(i);
        }
        break;
    }
    // Insert new item.
    if (type != E820_HOLE)
        insert_e820(i, start, size, type);
    //dump_map();
}

// Prep for memmap stuff - init bios table locations.
void
memmap_setup()
{
    e820_count = 0;
}

// Report on final memory locations.
void
memmap_finalize()
{
    dump_map();
}


/****************************************************************
 * malloc
 ****************************************************************/

#define MINALIGN 16

struct zone_s {
    u32 top, bottom, cur;
};

static struct zone_s ZoneHigh, ZoneFSeg;

static void *
__malloc(struct zone_s *zone, u32 size)
{
    u32 newpos = (zone->cur - size) / MINALIGN * MINALIGN;
    if (newpos < zone->bottom)
        // No space
        return NULL;
    zone->cur = newpos;
    return (void*)newpos;
}

// Allocate memory at the top of 32bit ram.
void *
malloc_high(u32 size)
{
    return __malloc(&ZoneHigh, size);
}

// Allocate memory in the 0xf0000-0x100000 area of ram.
void *
malloc_fseg(u32 size)
{
    return __malloc(&ZoneFSeg, size);
}

void
malloc_setup()
{
    // Memory in 0xf0000 area.
    memset(BiosTableSpace, 0, CONFIG_MAX_BIOSTABLE);
    ZoneFSeg.bottom = (u32)BiosTableSpace;
    ZoneFSeg.top = ZoneFSeg.cur = ZoneFSeg.bottom + CONFIG_MAX_BIOSTABLE;

    // Find memory at the top of ram.
    u32 top = 0;
    int i;
    for (i=e820_count-1; i>=0; i--) {
        struct e820entry *e = &e820_list[i];
        u64 end = e->start + e->size;
        if (e->type != E820_RAM || end > 0xffffffff)
            continue;
        top = end;
        break;
    }
    if (top < 1024*1024 + CONFIG_MAX_HIGHTABLE) {
        // No memory above 1Meg
        memset(&ZoneHigh, 0, sizeof(ZoneHigh));
        return;
    }
    ZoneHigh.bottom = top - CONFIG_MAX_HIGHTABLE;
    ZoneHigh.top = ZoneHigh.cur = ZoneHigh.bottom + CONFIG_MAX_HIGHTABLE;
    add_e820(ZoneHigh.bottom, CONFIG_MAX_HIGHTABLE, E820_RESERVED);
}

void
malloc_finalize()
{
    // Give back unused high ram.
    u32 giveback = (ZoneHigh.cur - ZoneHigh.bottom) / 4096 * 4096;
    add_e820(ZoneHigh.bottom, giveback, E820_RAM);

    // Report statistics
    u32 used = ZoneFSeg.top - ZoneFSeg.cur;
    u32 avail = ZoneFSeg.top - ZoneFSeg.bottom;
    dprintf(1, "malloc_fseg used=%d (%d%%)\n"
            , used, (100 * used) / avail);
    used = ZoneHigh.top - ZoneHigh.cur;
    avail = ZoneHigh.top - ZoneHigh.bottom;
    dprintf(1, "malloc_high used=%d (%d%%) (returned %d)\n"
            , used, (100 * used) / avail, giveback);
}
