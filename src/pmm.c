// Post memory manager (PMM) calls
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // checksum
#include "config.h" // BUILD_BIOS_ADDR
#include "memmap.h" // struct e820entry
#include "farptr.h" // GET_FARVAR
#include "biosvar.h" // GET_BDA
#include "optionroms.h" // OPTION_ROM_ALIGN
#include "list.h" // hlist_node

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
allocSpace(struct zone_s *zone, u32 size, u32 align, struct allocinfo_s *fill)
{
    struct allocinfo_s *info;
    hlist_for_each_entry(info, &zone->head, node) {
        void *dataend = info->dataend;
        void *allocend = info->allocend;
        void *newallocend = (void*)ALIGN_DOWN((u32)allocend - size, align);
        if (newallocend >= dataend && newallocend <= allocend) {
            // Found space - now reserve it.
            if (!fill)
                fill = newallocend;
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

// Release space allocated with allocSpace()
static void
freeSpace(struct allocinfo_s *info)
{
    struct allocinfo_s *next = container_of_or_null(
        info->node.next, struct allocinfo_s, node);
    if (next && next->allocend == info->data)
        next->allocend = info->allocend;
    hlist_del(&info->node);
}

// Add new memory to a zone
static void
addSpace(struct zone_s *zone, void *start, void *end)
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
    tempdetail.datainfo.data = tempdetail.datainfo.dataend = start;
    tempdetail.datainfo.allocend = end;
    hlist_add(&tempdetail.datainfo.node, pprev);

    // Allocate final allocation info.
    struct allocdetail_s *detail = allocSpace(
        &ZoneTmpHigh, sizeof(*detail), MALLOC_MIN_ALIGN, NULL);
    if (!detail) {
        detail = allocSpace(&ZoneTmpLow, sizeof(*detail)
                            , MALLOC_MIN_ALIGN, NULL);
        if (!detail) {
            hlist_del(&tempdetail.datainfo.node);
            warn_noalloc();
            return;
        }
    }

    // Replace temp alloc space with final alloc space
    pprev = tempdetail.datainfo.node.pprev;
    hlist_del(&tempdetail.datainfo.node);
    memcpy(&detail->datainfo, &tempdetail.datainfo, sizeof(detail->datainfo));
    detail->handle = PMM_DEFAULT_HANDLE;
    hlist_add(&detail->datainfo.node, pprev);
}

// Search all zones for an allocation obtained from allocSpace()
static struct allocinfo_s *
findAlloc(void *data)
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

// Return the last sentinal node of a zone
static struct allocinfo_s *
findLast(struct zone_s *zone)
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
        void *data = allocSpace(&ZoneLow, size, align, fill);
        if (data)
            return data;
    }

    struct allocinfo_s *info = findLast(&ZoneLow);
    if (!info)
        return NULL;
    u32 oldpos = (u32)info->allocend;
    u32 newpos = ALIGN_DOWN(oldpos - size, align);
    u32 bottom = (u32)info->dataend;
    if (newpos >= bottom && newpos <= oldpos)
        // Space already present.
        return allocSpace(&ZoneLow, size, align, fill);
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
        addSpace(&ZoneLow, (void*)newbottom, (void*)ebda_end);

    return allocSpace(&ZoneLow, size, align, fill);
}


/****************************************************************
 * tracked memory allocations
 ****************************************************************/

// Allocate memory from the given zone and track it as a PMM allocation
void * __malloc
pmm_malloc(struct zone_s *zone, u32 handle, u32 size, u32 align)
{
    ASSERT32FLAT();
    if (!size)
        return NULL;

    // Find and reserve space for bookkeeping.
    struct allocdetail_s *detail = allocSpace(
        &ZoneTmpHigh, sizeof(*detail), MALLOC_MIN_ALIGN, NULL);
    if (!detail) {
        detail = allocSpace(&ZoneTmpLow, sizeof(*detail)
                            , MALLOC_MIN_ALIGN, NULL);
        if (!detail)
            return NULL;
    }

    // Find and reserve space for main allocation
    void *data = allocSpace(zone, size, align, &detail->datainfo);
    if (!CONFIG_MALLOC_UPPERMEMORY && !data && zone == &ZoneLow)
        data = zonelow_expand(size, align, &detail->datainfo);
    if (!data) {
        freeSpace(&detail->detailinfo);
        return NULL;
    }

    dprintf(8, "pmm_malloc zone=%p handle=%x size=%d align=%x"
            " ret=%p (detail=%p)\n"
            , zone, handle, size, align
            , data, detail);
    detail->handle = handle;

    return data;
}

// Free a data block allocated with pmm_malloc
int
pmm_free(void *data)
{
    ASSERT32FLAT();
    struct allocinfo_s *info = findAlloc(data);
    if (!info || data == (void*)info || data == info->dataend)
        return -1;
    struct allocdetail_s *detail = container_of(
        info, struct allocdetail_s, datainfo);
    dprintf(8, "pmm_free %p (detail=%p)\n", data, detail);
    freeSpace(info);
    freeSpace(&detail->detailinfo);
    return 0;
}

// Find the amount of free space in a given zone.
static u32
pmm_getspace(struct zone_s *zone)
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

// Find the data block allocated with pmm_malloc with a given handle.
static void *
pmm_find(u32 handle)
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
    extern u8 code32init_end[];
    u32 end = (u32)code32init_end;
    return end > BUILD_BIOS_ADDR ? BUILD_BIOS_ADDR : end;
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
    if (!CONFIG_MALLOC_UPPERMEMORY) {
        if (RomEnd + size > rom_get_max())
            return NULL;
        return (void*)RomEnd;
    }
    u32 newend = ALIGN(RomEnd + size, OPTION_ROM_ALIGN) + OPROM_HEADER_RESERVE;
    if (newend > (u32)RomBase->allocend)
        return NULL;
    if (newend < (u32)zonelow_base + OPROM_HEADER_RESERVE)
        newend = (u32)zonelow_base + OPROM_HEADER_RESERVE;
    RomBase->data = RomBase->dataend = (void*)newend;
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
    add_e820(BUILD_LOWRAM_END, BUILD_BIOS_ADDR-BUILD_LOWRAM_END, E820_HOLE);

    // Mark known areas as reserved.
    add_e820(BUILD_BIOS_ADDR, BUILD_BIOS_SIZE, E820_RESERVED);

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
        addSpace(&ZoneTmpHigh, (void*)s, (void*)e);
    }

    // Populate regions
    addSpace(&ZoneTmpLow, (void*)BUILD_STACK_ADDR, (void*)BUILD_EBDA_MINIMUM);
    if (highram) {
        addSpace(&ZoneHigh, (void*)highram
                 , (void*)highram + BUILD_MAX_HIGHTABLE);
        add_e820(highram, BUILD_MAX_HIGHTABLE, E820_RESERVED);
    }
}

void
csm_malloc_preinit(u32 low_pmm, u32 low_pmm_size, u32 hi_pmm, u32 hi_pmm_size)
{
    ASSERT32FLAT();

    if (hi_pmm_size > BUILD_MAX_HIGHTABLE) {
        void *hi_pmm_end = (void *)hi_pmm + hi_pmm_size;
        addSpace(&ZoneTmpHigh, (void *)hi_pmm, hi_pmm_end - BUILD_MAX_HIGHTABLE);
        addSpace(&ZoneHigh, hi_pmm_end - BUILD_MAX_HIGHTABLE, hi_pmm_end);
    } else {
        addSpace(&ZoneTmpHigh, (void *)hi_pmm, (void *)hi_pmm + hi_pmm_size);
    }
    addSpace(&ZoneTmpLow, (void *)low_pmm, (void *)low_pmm + low_pmm_size);
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
        addSpace(&ZoneLow, zonelow_base + OPROM_HEADER_RESERVE
                 , final_varlow_start);
        RomBase = findLast(&ZoneLow);
    } else {
        addSpace(&ZoneLow, (void*)ALIGN_DOWN((u32)final_varlow_start, 1024)
                 , final_varlow_start);
    }

    // Add space available in f-segment to ZoneFSeg
    extern u8 zonefseg_start[], zonefseg_end[];
    memset(zonefseg_start, 0, zonefseg_end - zonefseg_start);
    addSpace(&ZoneFSeg, zonefseg_start, zonefseg_end);

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
    add_e820(endlow, BUILD_LOWRAM_END-endlow, E820_RESERVED);

    // Clear unused f-seg ram.
    struct allocinfo_s *info = findLast(&ZoneFSeg);
    memset(info->dataend, 0, info->allocend - info->dataend);
    dprintf(1, "Space available for UMB: %x-%x, %x-%x\n"
            , RomEnd, base, (u32)info->dataend, (u32)info->allocend);

    // Give back unused high ram.
    info = findLast(&ZoneHigh);
    if (info) {
        u32 giveback = ALIGN_DOWN(info->allocend - info->dataend, PAGE_SIZE);
        add_e820((u32)info->dataend, giveback, E820_RAM);
        dprintf(1, "Returned %d bytes of ZoneHigh\n", giveback);
    }

    calcRamSize();
}


/****************************************************************
 * pmm interface
 ****************************************************************/

struct pmmheader {
    u32 signature;
    u8 version;
    u8 length;
    u8 checksum;
    struct segoff_s entry;
    u8 reserved[5];
} PACKED;

extern struct pmmheader PMMHEADER;

#define PMM_SIGNATURE 0x4d4d5024 // $PMM

#if CONFIG_PMM
struct pmmheader PMMHEADER __aligned(16) VARFSEG = {
    .signature = PMM_SIGNATURE,
    .version = 0x01,
    .length = sizeof(PMMHEADER),
};
#endif

#define PMM_FUNCTION_NOT_SUPPORTED 0xffffffff

// PMM - allocate
static u32
handle_pmm00(u16 *args)
{
    u32 length = *(u32*)&args[1], handle = *(u32*)&args[3];
    u16 flags = args[5];
    dprintf(3, "pmm00: length=%x handle=%x flags=%x\n"
            , length, handle, flags);
    struct zone_s *lowzone = &ZoneTmpLow, *highzone = &ZoneTmpHigh;
    if (flags & 8) {
        // Permanent memory request.
        lowzone = &ZoneLow;
        highzone = &ZoneHigh;
    }
    if (!length) {
        // Memory size request
        switch (flags & 3) {
        default:
        case 0:
            return 0;
        case 1:
            return pmm_getspace(lowzone);
        case 2:
            return pmm_getspace(highzone);
        case 3: {
            u32 spacelow = pmm_getspace(lowzone);
            u32 spacehigh = pmm_getspace(highzone);
            if (spacelow > spacehigh)
                return spacelow;
            return spacehigh;
        }
        }
    }
    u32 size = length * 16;
    if ((s32)size <= 0)
        return 0;
    u32 align = MALLOC_MIN_ALIGN;
    if (flags & 4) {
        align = 1<<__ffs(size);
        if (align < MALLOC_MIN_ALIGN)
            align = MALLOC_MIN_ALIGN;
    }
    switch (flags & 3) {
    default:
    case 0:
        return 0;
    case 1:
        return (u32)pmm_malloc(lowzone, handle, size, align);
    case 2:
        return (u32)pmm_malloc(highzone, handle, size, align);
    case 3: {
        void *data = pmm_malloc(lowzone, handle, size, align);
        if (data)
            return (u32)data;
        return (u32)pmm_malloc(highzone, handle, size, align);
    }
    }
}

// PMM - find
static u32
handle_pmm01(u16 *args)
{
    u32 handle = *(u32*)&args[1];
    dprintf(3, "pmm01: handle=%x\n", handle);
    if (handle == PMM_DEFAULT_HANDLE)
        return 0;
    return (u32)pmm_find(handle);
}

// PMM - deallocate
static u32
handle_pmm02(u16 *args)
{
    u32 buffer = *(u32*)&args[1];
    dprintf(3, "pmm02: buffer=%x\n", buffer);
    int ret = pmm_free((void*)buffer);
    if (ret)
        // Error
        return 1;
    return 0;
}

static u32
handle_pmmXX(u16 *args)
{
    return PMM_FUNCTION_NOT_SUPPORTED;
}

u32 VISIBLE32INIT
handle_pmm(u16 *args)
{
    ASSERT32FLAT();
    if (! CONFIG_PMM)
        return PMM_FUNCTION_NOT_SUPPORTED;

    u16 arg1 = args[0];
    dprintf(DEBUG_HDL_pmm, "pmm call arg1=%x\n", arg1);

    u32 ret;
    switch (arg1) {
    case 0x00: ret = handle_pmm00(args); break;
    case 0x01: ret = handle_pmm01(args); break;
    case 0x02: ret = handle_pmm02(args); break;
    default:   ret = handle_pmmXX(args); break;
    }

    return ret;
}

void
pmm_init(void)
{
    if (! CONFIG_PMM)
        return;

    dprintf(3, "init PMM\n");

    PMMHEADER.entry = FUNC16(entry_pmm);
    PMMHEADER.checksum -= checksum(&PMMHEADER, sizeof(PMMHEADER));
}

void
pmm_prepboot(void)
{
    if (! CONFIG_PMM)
        return;

    dprintf(3, "finalize PMM\n");

    PMMHEADER.signature = 0;
    PMMHEADER.entry.segoff = 0;
}
