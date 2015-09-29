// Internal dynamic memory allocations.
//
// Copyright (C) 2009-2013  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "biosvar.h" // GET_BDA
#include "config.h" // BUILD_BIOS_ADDR
#include "e820map.h" // struct e820entry
#include "list.h" // hlist_node
#include "malloc.h" // _malloc
#include "memmap.h" // PAGE_SIZE
#include "output.h" // dprintf
#include "stacks.h" // wait_preempt
#include "std/optionrom.h" // OPTION_ROM_ALIGN
#include "string.h" // memset

// Information on a reserved area.
struct allocinfo_s {
    struct hlist_node node;
    void *data, *dataend, *allocend;
};

// Information on a tracked memory allocation.
struct allocdetail_s {
    struct allocinfo_s detailinfo;
    struct allocinfo_s datainfo;
    u32 handle;
};

// The various memory zones.
struct zone_s {
    struct hlist_head head;
};

struct zone_s ZoneLow VARVERIFY32INIT, ZoneHigh VARVERIFY32INIT;
struct zone_s ZoneFSeg VARVERIFY32INIT;
struct zone_s ZoneTmpLow VARVERIFY32INIT, ZoneTmpHigh VARVERIFY32INIT;

static struct zone_s *Zones[] VARVERIFY32INIT = {
    &ZoneTmpLow, &ZoneLow, &ZoneFSeg, &ZoneTmpHigh, &ZoneHigh
};


/****************************************************************
 * low-level memory reservations
 ****************************************************************/

// Find and reserve space from a given zone
static void *
alloc_new(struct zone_s *zone, u32 size, u32 align, struct allocinfo_s *fill)
{
    struct allocinfo_s *info;
    hlist_for_each_entry(info, &zone->head, node) {
        void *dataend = info->dataend;
        void *allocend = info->allocend;
        void *newallocend = (void*)ALIGN_DOWN((u32)allocend - size, align);
        if (newallocend >= dataend && newallocend <= allocend) {
            // Found space - now reserve it.
            fill->data = newallocend;
            fill->dataend = newallocend + size;
            fill->allocend = allocend;

            info->allocend = newallocend;
            hlist_add_before(&fill->node, &info->node);
            return newallocend;
        }
    }
    return NULL;
}

// Reserve space for a 'struct allocdetail_s' and fill
static struct allocdetail_s *
alloc_new_detail(struct allocdetail_s *temp)
{
    struct allocdetail_s *detail = alloc_new(
        &ZoneTmpHigh, sizeof(*detail), MALLOC_MIN_ALIGN, &temp->detailinfo);
    if (!detail) {
        detail = alloc_new(&ZoneTmpLow, sizeof(*detail)
                           , MALLOC_MIN_ALIGN, &temp->detailinfo);
        if (!detail) {
            warn_noalloc();
            return NULL;
        }
    }

    // Fill final 'detail' allocation from data in 'temp'
    memcpy(detail, temp, sizeof(*detail));
    hlist_replace(&temp->detailinfo.node, &detail->detailinfo.node);
    hlist_replace(&temp->datainfo.node, &detail->datainfo.node);
    return detail;
}

// Add new memory to a zone
static void
alloc_add(struct zone_s *zone, void *start, void *end)
{
    // Find position to add space
    struct allocinfo_s *info;
    struct hlist_node **pprev;
    hlist_for_each_entry_pprev(info, pprev, &zone->head, node) {
        if (info->data < start)
            break;
    }

    // Add space using temporary allocation info.
    struct allocdetail_s tempdetail;
    tempdetail.handle = MALLOC_DEFAULT_HANDLE;
    tempdetail.datainfo.data = tempdetail.datainfo.dataend = start;
    tempdetail.datainfo.allocend = end;
    hlist_add(&tempdetail.datainfo.node, pprev);

    // Allocate final allocation info.
    struct allocdetail_s *detail = alloc_new_detail(&tempdetail);
    if (!detail)
        hlist_del(&tempdetail.datainfo.node);
}

// Release space allocated with alloc_new()
static void
alloc_free(struct allocinfo_s *info)
{
    struct allocinfo_s *next = container_of_or_null(
        info->node.next, struct allocinfo_s, node);
    if (next && next->allocend == info->data)
        next->allocend = info->allocend;
    hlist_del(&info->node);
}

// Search all zones for an allocation obtained from alloc_new()
static struct allocinfo_s *
alloc_find(void *data)
{
    int i;
    for (i=0; i<ARRAY_SIZE(Zones); i++) {
        struct allocinfo_s *info;
        hlist_for_each_entry(info, &Zones[i]->head, node) {
            if (info->data == data)
                return info;
        }
    }
    return NULL;
}

// Find the lowest memory range added by alloc_add()
static struct allocinfo_s *
alloc_find_lowest(struct zone_s *zone)
{
    struct allocinfo_s *info, *last = NULL;
    hlist_for_each_entry(info, &zone->head, node) {
        last = info;
    }
    return last;
}


/****************************************************************
 * ebda movement
 ****************************************************************/

// Move ebda
static int
relocate_ebda(u32 newebda, u32 oldebda, u8 ebda_size)
{
    u32 lowram = GET_BDA(mem_size_kb) * 1024;
    if (oldebda != lowram)
        // EBDA isn't at end of ram - give up.
        return -1;

    // Do copy
    memmove((void*)newebda, (void*)oldebda, ebda_size * 1024);

    // Update indexes
    dprintf(1, "ebda moved from %x to %x\n", oldebda, newebda);
    SET_BDA(mem_size_kb, newebda / 1024);
    SET_BDA(ebda_seg, FLATPTR_TO_SEG(newebda));
    return 0;
}

// Support expanding the ZoneLow dynamically.
static void *
zonelow_expand(u32 size, u32 align, struct allocinfo_s *fill)
{
    // Make sure to not move ebda while an optionrom is running.
    if (unlikely(wait_preempt())) {
        void *data = alloc_new(&ZoneLow, size, align, fill);
        if (data)
            return data;
    }

    struct allocinfo_s *info = alloc_find_lowest(&ZoneLow);
    if (!info)
        return NULL;
    u32 oldpos = (u32)info->allocend;
    u32 newpos = ALIGN_DOWN(oldpos - size, align);
    u32 bottom = (u32)info->dataend;
    if (newpos >= bottom && newpos <= oldpos)
        // Space already present.
        return alloc_new(&ZoneLow, size, align, fill);
    u16 ebda_seg = get_ebda_seg();
    u32 ebda_pos = (u32)MAKE_FLATPTR(ebda_seg, 0);
    u8 ebda_size = GET_EBDA(ebda_seg, size);
    u32 ebda_end = ebda_pos + ebda_size * 1024;
    if (ebda_end != bottom)
        // Something else is after ebda - can't use any existing space.
        newpos = ALIGN_DOWN(ebda_end - size, align);
    u32 newbottom = ALIGN_DOWN(newpos, 1024);
    u32 newebda = ALIGN_DOWN(newbottom - ebda_size * 1024, 1024);
    if (newebda < BUILD_EBDA_MINIMUM)
        // Not enough space.
        return NULL;

    // Move ebda
    int ret = relocate_ebda(newebda, ebda_pos, ebda_size);
    if (ret)
        return NULL;

    // Update zone
    if (ebda_end == bottom) {
        info->data = (void*)newbottom;
        info->dataend = (void*)newbottom;
    } else
        alloc_add(&ZoneLow, (void*)newbottom, (void*)ebda_end);

    return alloc_new(&ZoneLow, size, align, fill);
}


/****************************************************************
 * tracked memory allocations
 ****************************************************************/

// Allocate memory from the given zone and track it as a PMM allocation
void * __malloc
_malloc(struct zone_s *zone, u32 size, u32 align)
{
    ASSERT32FLAT();
    if (!size)
        return NULL;

    // Find and reserve space for main allocation
    struct allocdetail_s tempdetail;
    tempdetail.handle = MALLOC_DEFAULT_HANDLE;
    void *data = alloc_new(zone, size, align, &tempdetail.datainfo);
    if (!CONFIG_MALLOC_UPPERMEMORY && !data && zone == &ZoneLow)
        data = zonelow_expand(size, align, &tempdetail.datainfo);
    if (!data)
        return NULL;

    // Find and reserve space for bookkeeping.
    struct allocdetail_s *detail = alloc_new_detail(&tempdetail);
    if (!detail) {
        alloc_free(&tempdetail.datainfo);
        return NULL;
    }

    dprintf(8, "_malloc zone=%p size=%d align=%x ret=%p (detail=%p)\n"
            , zone, size, align, data, detail);

    return data;
}

// Free a data block allocated with _malloc
int
_free(void *data)
{
    ASSERT32FLAT();
    struct allocinfo_s *info = alloc_find(data);
    if (!info || data == (void*)info || data == info->dataend)
        return -1;
    struct allocdetail_s *detail = container_of(
        info, struct allocdetail_s, datainfo);
    dprintf(8, "_free %p (detail=%p)\n", data, detail);
    alloc_free(info);
    alloc_free(&detail->detailinfo);
    return 0;
}

// Find the amount of free space in a given zone.
u32
malloc_getspace(struct zone_s *zone)
{
    // XXX - doesn't account for ZoneLow being able to grow.
    // XXX - results not reliable when CONFIG_THREAD_OPTIONROMS
    u32 maxspace = 0;
    struct allocinfo_s *info;
    hlist_for_each_entry(info, &zone->head, node) {
        u32 space = info->allocend - info->dataend;
        if (space > maxspace)
            maxspace = space;
    }

    if (zone != &ZoneTmpHigh && zone != &ZoneTmpLow)
        return maxspace;
    // Account for space needed for PMM tracking.
    u32 reserve = ALIGN(sizeof(struct allocdetail_s), MALLOC_MIN_ALIGN);
    if (maxspace <= reserve)
        return 0;
    return maxspace - reserve;
}

// Set a handle associated with an allocation.
void
malloc_sethandle(void *data, u32 handle)
{
    ASSERT32FLAT();
    struct allocinfo_s *info = alloc_find(data);
    if (!info || data == (void*)info || data == info->dataend)
        return;
    struct allocdetail_s *detail = container_of(
        info, struct allocdetail_s, datainfo);
    detail->handle = handle;
}

// Find the data block allocated with _malloc with a given handle.
void *
malloc_findhandle(u32 handle)
{
    int i;
    for (i=0; i<ARRAY_SIZE(Zones); i++) {
        struct allocinfo_s *info;
        hlist_for_each_entry(info, &Zones[i]->head, node) {
            if (info->data != (void*)info)
                continue;
            struct allocdetail_s *detail = container_of(
                info, struct allocdetail_s, detailinfo);
            if (detail->handle == handle)
                return detail->datainfo.data;
        }
    }
    return NULL;
}


/****************************************************************
 * 0xc0000-0xf0000 management
 ****************************************************************/

static u32 RomEnd = BUILD_ROM_START;
static struct allocinfo_s *RomBase;

#define OPROM_HEADER_RESERVE 16

// Return the maximum memory position option roms may use.
u32
rom_get_max(void)
{
    if (CONFIG_MALLOC_UPPERMEMORY)
        return ALIGN_DOWN((u32)RomBase->allocend - OPROM_HEADER_RESERVE
                          , OPTION_ROM_ALIGN);
    extern u8 final_readonly_start[];
    return (u32)final_readonly_start;
}

// Return the end of the last deployed option rom.
u32
rom_get_last(void)
{
    return RomEnd;
}

// Request space for an optionrom in 0xc0000-0xf0000 area.
struct rom_header *
rom_reserve(u32 size)
{
    u32 newend = ALIGN(RomEnd + size, OPTION_ROM_ALIGN);
    if (newend > rom_get_max())
        return NULL;
    if (CONFIG_MALLOC_UPPERMEMORY) {
        if (newend < (u32)zonelow_base)
            newend = (u32)zonelow_base;
        RomBase->data = RomBase->dataend = (void*)newend + OPROM_HEADER_RESERVE;
    }
    return (void*)RomEnd;
}

// Confirm space as in use by an optionrom.
int
rom_confirm(u32 size)
{
    void *new = rom_reserve(size);
    if (!new) {
        warn_noalloc();
        return -1;
    }
    RomEnd = ALIGN(RomEnd + size, OPTION_ROM_ALIGN);
    return 0;
}


/****************************************************************
 * Setup
 ****************************************************************/

void
malloc_preinit(void)
{
    ASSERT32FLAT();
    dprintf(3, "malloc preinit\n");

    // Don't declare any memory between 0xa0000 and 0x100000
    e820_remove(BUILD_LOWRAM_END, BUILD_BIOS_ADDR-BUILD_LOWRAM_END);

    // Mark known areas as reserved.
    e820_add(BUILD_BIOS_ADDR, BUILD_BIOS_SIZE, E820_RESERVED);

    // Populate temp high ram
    u32 highram = 0;
    int i;
    for (i=e820_count-1; i>=0; i--) {
        struct e820entry *en = &e820_list[i];
        u64 end = en->start + en->size;
        if (end < 1024*1024)
            break;
        if (en->type != E820_RAM || end > 0xffffffff)
            continue;
        u32 s = en->start, e = end;
        if (!highram) {
            u32 newe = ALIGN_DOWN(e - BUILD_MAX_HIGHTABLE, MALLOC_MIN_ALIGN);
            if (newe <= e && newe >= s) {
                highram = newe;
                e = newe;
            }
        }
        alloc_add(&ZoneTmpHigh, (void*)s, (void*)e);
    }

    // Populate regions
    alloc_add(&ZoneTmpLow, (void*)BUILD_STACK_ADDR, (void*)BUILD_EBDA_MINIMUM);
    if (highram) {
        alloc_add(&ZoneHigh, (void*)highram
                  , (void*)highram + BUILD_MAX_HIGHTABLE);
        e820_add(highram, BUILD_MAX_HIGHTABLE, E820_RESERVED);
    }
}

void
csm_malloc_preinit(u32 low_pmm, u32 low_pmm_size, u32 hi_pmm, u32 hi_pmm_size)
{
    ASSERT32FLAT();

    if (hi_pmm_size > BUILD_MAX_HIGHTABLE) {
        void *hi_pmm_end = (void*)hi_pmm + hi_pmm_size;
        alloc_add(&ZoneTmpHigh, (void*)hi_pmm, hi_pmm_end - BUILD_MAX_HIGHTABLE);
        alloc_add(&ZoneHigh, hi_pmm_end - BUILD_MAX_HIGHTABLE, hi_pmm_end);
    } else {
        alloc_add(&ZoneTmpHigh, (void*)hi_pmm, (void*)hi_pmm + hi_pmm_size);
    }
    alloc_add(&ZoneTmpLow, (void*)low_pmm, (void*)low_pmm + low_pmm_size);
}

u32 LegacyRamSize VARFSEG;

// Calculate the maximum ramsize (less than 4gig) from e820 map.
static void
calcRamSize(void)
{
    u32 rs = 0;
    int i;
    for (i=e820_count-1; i>=0; i--) {
        struct e820entry *en = &e820_list[i];
        u64 end = en->start + en->size;
        u32 type = en->type;
        if (end <= 0xffffffff && (type == E820_ACPI || type == E820_RAM)) {
            rs = end;
            break;
        }
    }
    LegacyRamSize = rs >= 1024*1024 ? rs : 1024*1024;
}

// Update pointers after code relocation.
void
malloc_init(void)
{
    ASSERT32FLAT();
    dprintf(3, "malloc init\n");

    if (CONFIG_RELOCATE_INIT) {
        // Fixup malloc pointers after relocation
        int i;
        for (i=0; i<ARRAY_SIZE(Zones); i++) {
            struct zone_s *zone = Zones[i];
            if (zone->head.first)
                zone->head.first->pprev = &zone->head.first;
        }
    }

    // Initialize low-memory region
    extern u8 varlow_start[], varlow_end[], final_varlow_start[];
    memmove(final_varlow_start, varlow_start, varlow_end - varlow_start);
    if (CONFIG_MALLOC_UPPERMEMORY) {
        alloc_add(&ZoneLow, zonelow_base + OPROM_HEADER_RESERVE
                  , final_varlow_start);
        RomBase = alloc_find_lowest(&ZoneLow);
    } else {
        alloc_add(&ZoneLow, (void*)ALIGN_DOWN((u32)final_varlow_start, 1024)
                  , final_varlow_start);
    }

    // Add space available in f-segment to ZoneFSeg
    extern u8 zonefseg_start[], zonefseg_end[];
    memset(zonefseg_start, 0, zonefseg_end - zonefseg_start);
    alloc_add(&ZoneFSeg, zonefseg_start, zonefseg_end);

    calcRamSize();
}

void
malloc_prepboot(void)
{
    ASSERT32FLAT();
    dprintf(3, "malloc finalize\n");

    u32 base = rom_get_max();
    memset((void*)RomEnd, 0, base-RomEnd);
    if (CONFIG_MALLOC_UPPERMEMORY) {
        // Place an optionrom signature around used low mem area.
        struct rom_header *dummyrom = (void*)base;
        dummyrom->signature = OPTION_ROM_SIGNATURE;
        int size = (BUILD_BIOS_ADDR - base) / 512;
        dummyrom->size = (size > 255) ? 255 : size;
    }

    // Reserve more low-mem if needed.
    u32 endlow = GET_BDA(mem_size_kb)*1024;
    e820_add(endlow, BUILD_LOWRAM_END-endlow, E820_RESERVED);

    // Clear unused f-seg ram.
    struct allocinfo_s *info = alloc_find_lowest(&ZoneFSeg);
    memset(info->dataend, 0, info->allocend - info->dataend);
    dprintf(1, "Space available for UMB: %x-%x, %x-%x\n"
            , RomEnd, base, (u32)info->dataend, (u32)info->allocend);

    // Give back unused high ram.
    info = alloc_find_lowest(&ZoneHigh);
    if (info) {
        u32 giveback = ALIGN_DOWN(info->allocend - info->dataend, PAGE_SIZE);
        e820_add((u32)info->dataend, giveback, E820_RAM);
        dprintf(1, "Returned %d bytes of ZoneHigh\n", giveback);
    }

    calcRamSize();
}
