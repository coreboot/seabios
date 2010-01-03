// Post memory manager (PMM) calls
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // checksum
#include "config.h" // BUILD_BIOS_ADDR
#include "memmap.h" // find_high_area
#include "farptr.h" // GET_FARVAR
#include "biosvar.h" // GET_BDA


#if MODESEGMENT
// The 16bit pmm entry points runs in "big real" mode, and can
// therefore read/write to the 32bit malloc variables.
#define GET_PMMVAR(var) ({                      \
            SET_SEG(ES, 0);                     \
            __GET_VAR("addr32 ", ES, (var)); })
#define SET_PMMVAR(var, val) do {               \
        SET_SEG(ES, 0);                         \
        __SET_VAR("addr32 ", ES, (var), (val)); \
    } while (0)
#else
#define GET_PMMVAR(var) (var)
#define SET_PMMVAR(var, val) do { (var) = (val); } while (0)
#endif

// Zone definitions
struct zone_s {
    u32 top, bottom, cur;
};

struct zone_s ZoneLow VAR32FLATVISIBLE, ZoneHigh VAR32FLATVISIBLE;
struct zone_s ZoneFSeg VAR32FLATVISIBLE;
struct zone_s ZoneTmpLow VAR32FLATVISIBLE, ZoneTmpHigh VAR32FLATVISIBLE;

struct zone_s *Zones[] VAR32FLATVISIBLE = {
    &ZoneTmpLow, &ZoneLow, &ZoneFSeg, &ZoneTmpHigh, &ZoneHigh
};


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
    if (MODESEGMENT)
        memcpy_far(FLATPTR_TO_SEG(newebda)
                   , (void*)FLATPTR_TO_OFFSET(newebda)
                   , FLATPTR_TO_SEG(oldebda)
                   , (void*)FLATPTR_TO_OFFSET(oldebda)
                   , ebda_size * 1024);
    else
        memmove((void*)newebda, (void*)oldebda, ebda_size * 1024);

    // Update indexes
    dprintf(1, "ebda moved from %x to %x\n", oldebda, newebda);
    SET_BDA(mem_size_kb, newebda / 1024);
    SET_BDA(ebda_seg, FLATPTR_TO_SEG(newebda));
    return 0;
}

// Support expanding the ZoneLow dynamically.
static void
zonelow_expand(u32 size, u32 align)
{
    u32 oldpos = GET_PMMVAR(ZoneLow.cur);
    u32 newpos = ALIGN_DOWN(oldpos - size, align);
    u32 bottom = GET_PMMVAR(ZoneLow.bottom);
    if (newpos >= bottom && newpos <= oldpos)
        // Space already present.
        return;
    u16 ebda_seg = get_ebda_seg();
    u32 ebda_pos = (u32)MAKE_FLATPTR(ebda_seg, 0);
    u8 ebda_size = GET_EBDA2(ebda_seg, size);
    u32 ebda_end = ebda_pos + ebda_size * 1024;
    if (ebda_end != bottom) {
        // Something else is after ebda - can't use any existing space.
        oldpos = ebda_end;
        newpos = ALIGN_DOWN(oldpos - size, align);
    }
    u32 newbottom = ALIGN_DOWN(newpos, 1024);
    u32 newebda = ALIGN_DOWN(newbottom - ebda_size * 1024, 1024);
    if (newebda < BUILD_EBDA_MINIMUM)
        // Not enough space.
        return;

    // Move ebda
    int ret = relocate_ebda(newebda, ebda_pos, ebda_size);
    if (ret)
        return;

    // Update zone
    SET_PMMVAR(ZoneLow.cur, oldpos);
    SET_PMMVAR(ZoneLow.bottom, newbottom);
}


/****************************************************************
 * zone allocations
 ****************************************************************/

// Obtain memory from a given zone.
static void *
zone_malloc(struct zone_s *zone, u32 size, u32 align)
{
    u32 oldpos = GET_PMMVAR(zone->cur);
    u32 newpos = ALIGN_DOWN(oldpos - size, align);
    if (newpos < GET_PMMVAR(zone->bottom) || newpos > oldpos)
        // No space
        return NULL;
    SET_PMMVAR(zone->cur, newpos);
    return (void*)newpos;
}

// Find the zone that contains the given data block.
static struct zone_s *
zone_find(void *data)
{
    int i;
    for (i=0; i<ARRAY_SIZE(Zones); i++) {
        struct zone_s *zone = GET_PMMVAR(Zones[i]);
        if ((u32)data >= GET_PMMVAR(zone->cur)
            && (u32)data < GET_PMMVAR(zone->top))
            return zone;
    }
    return NULL;
}

// Return memory to a zone (if it was the last to be allocated).
static int
zone_free(void *data, u32 olddata)
{
    struct zone_s *zone = zone_find(data);
    if (!zone || !data || GET_PMMVAR(zone->cur) != (u32)data)
        return -1;
    SET_PMMVAR(zone->cur, olddata);
    return 0;
}

// Report the status of all the zones.
static void
dumpZones(void)
{
    int i;
    for (i=0; i<ARRAY_SIZE(Zones); i++) {
        struct zone_s *zone = Zones[i];
        u32 used = zone->top - zone->cur;
        u32 avail = zone->top - zone->bottom;
        u32 pct = avail ? ((100 * used) / avail) : 0;
        dprintf(2, "zone %d: %08x-%08x used=%d (%d%%)\n"
                , i, zone->bottom, zone->top, used, pct);
    }
}


/****************************************************************
 * tracked memory allocations
 ****************************************************************/

// Information on PMM tracked allocations
struct pmmalloc_s {
    void *data;
    u32 olddata;
    u32 handle;
    u32 oldallocdata;
    struct pmmalloc_s *next;
};

struct pmmalloc_s *PMMAllocs VAR32FLATVISIBLE;

// Allocate memory from the given zone and track it as a PMM allocation
void *
pmm_malloc(struct zone_s *zone, u32 handle, u32 size, u32 align)
{
    u32 oldallocdata = GET_PMMVAR(ZoneTmpHigh.cur);
    struct pmmalloc_s *info = zone_malloc(&ZoneTmpHigh, sizeof(*info)
                                          , MALLOC_MIN_ALIGN);
    if (!info) {
        oldallocdata = GET_PMMVAR(ZoneTmpLow.cur);
        info = zone_malloc(&ZoneTmpLow, sizeof(*info), MALLOC_MIN_ALIGN);
        if (!info)
            return NULL;
    }
    if (zone == &ZoneLow)
        zonelow_expand(size, align);
    u32 olddata = GET_PMMVAR(zone->cur);
    void *data = zone_malloc(zone, size, align);
    if (! data) {
        zone_free(info, oldallocdata);
        return NULL;
    }
    dprintf(8, "pmm_malloc zone=%p handle=%x size=%d align=%x"
            " ret=%p (info=%p)\n"
            , zone, handle, size, align
            , data, info);
    SET_PMMVAR(info->data, data);
    SET_PMMVAR(info->olddata, olddata);
    SET_PMMVAR(info->handle, handle);
    SET_PMMVAR(info->oldallocdata, oldallocdata);
    SET_PMMVAR(info->next, GET_PMMVAR(PMMAllocs));
    SET_PMMVAR(PMMAllocs, info);
    return data;
}

// Free a raw data block (either from a zone or from pmm alloc list).
static void
pmm_free_data(void *data, u32 olddata)
{
    int ret = zone_free(data, olddata);
    if (!ret)
        // Success - done.
        return;
    struct pmmalloc_s *info;
    for (info=GET_PMMVAR(PMMAllocs); info; info = GET_PMMVAR(info->next))
        if (GET_PMMVAR(info->olddata) == (u32)data) {
            SET_PMMVAR(info->olddata, olddata);
            return;
        } else if (GET_PMMVAR(info->oldallocdata) == (u32)data) {
            SET_PMMVAR(info->oldallocdata, olddata);
            return;
        }
}

// Free a data block allocated with pmm_malloc
int
pmm_free(void *data)
{
    struct pmmalloc_s **pinfo = &PMMAllocs;
    for (;;) {
        struct pmmalloc_s *info = GET_PMMVAR(*pinfo);
        if (!info)
            return -1;
        if (GET_PMMVAR(info->data) == data) {
            SET_PMMVAR(*pinfo, GET_PMMVAR(info->next));
            u32 oldallocdata = GET_PMMVAR(info->oldallocdata);
            u32 olddata = GET_PMMVAR(info->olddata);
            pmm_free_data(data, olddata);
            pmm_free_data(info, oldallocdata);
            dprintf(8, "pmm_free data=%p olddata=%p oldallocdata=%p info=%p\n"
                    , data, (void*)olddata, (void*)oldallocdata, info);
            return 0;
        }
        pinfo = &info->next;
    }
}

// Find the amount of free space in a given zone.
static u32
pmm_getspace(struct zone_s *zone)
{
    // XXX - doesn't account for ZoneLow being able to grow.
    u32 space = GET_PMMVAR(zone->cur) - GET_PMMVAR(zone->bottom);
    if (zone != &ZoneTmpHigh && zone != &ZoneTmpLow)
        return space;
    // Account for space needed for PMM tracking.
    u32 reserve = ALIGN(sizeof(struct pmmalloc_s), MALLOC_MIN_ALIGN);
    if (space <= reserve)
        return 0;
    return space - reserve;
}

// Find the data block allocated with pmm_malloc with a given handle.
static void *
pmm_find(u32 handle)
{
    struct pmmalloc_s *info;
    for (info=GET_PMMVAR(PMMAllocs); info; info = GET_PMMVAR(info->next))
        if (GET_PMMVAR(info->handle) == handle)
            return GET_PMMVAR(info->data);
    return NULL;
}

void
malloc_setup(void)
{
    ASSERT32FLAT();
    dprintf(3, "malloc setup\n");

    PMMAllocs = NULL;

    // Memory in 0xf0000 area.
    extern u8 code32flat_start[];
    if ((u32)code32flat_start > BUILD_BIOS_ADDR)
        // Clear unused parts of f-segment
        memset((void*)BUILD_BIOS_ADDR, 0
               , (u32)code32flat_start - BUILD_BIOS_ADDR);
    memset(BiosTableSpace, 0, CONFIG_MAX_BIOSTABLE);
    ZoneFSeg.bottom = (u32)BiosTableSpace;
    ZoneFSeg.top = ZoneFSeg.cur = ZoneFSeg.bottom + CONFIG_MAX_BIOSTABLE;

    // Memory under 1Meg.
    ZoneTmpLow.bottom = BUILD_STACK_ADDR;
    ZoneTmpLow.top = ZoneTmpLow.cur = BUILD_EBDA_MINIMUM;

    // Permanent memory under 1Meg.
    ZoneLow.bottom = ZoneLow.top = ZoneLow.cur = BUILD_LOWRAM_END;

    // Find memory at the top of ram.
    struct e820entry *e = find_high_area(CONFIG_MAX_HIGHTABLE+MALLOC_MIN_ALIGN);
    if (!e) {
        // No memory above 1Meg
        memset(&ZoneHigh, 0, sizeof(ZoneHigh));
        memset(&ZoneTmpHigh, 0, sizeof(ZoneTmpHigh));
        return;
    }
    u32 top = e->start + e->size, bottom = e->start;

    // Memory at top of ram.
    ZoneHigh.bottom = ALIGN(top - CONFIG_MAX_HIGHTABLE, MALLOC_MIN_ALIGN);
    ZoneHigh.top = ZoneHigh.cur = ZoneHigh.bottom + CONFIG_MAX_HIGHTABLE;
    add_e820(ZoneHigh.bottom, CONFIG_MAX_HIGHTABLE, E820_RESERVED);

    // Memory above 1Meg
    ZoneTmpHigh.bottom = ALIGN(bottom, MALLOC_MIN_ALIGN);
    ZoneTmpHigh.top = ZoneTmpHigh.cur = ZoneHigh.bottom;
}

void
malloc_finalize(void)
{
    dprintf(3, "malloc finalize\n");

    dumpZones();

    // Reserve more low-mem if needed.
    u32 endlow = GET_BDA(mem_size_kb)*1024;
    add_e820(endlow, BUILD_LOWRAM_END-endlow, E820_RESERVED);

    // Give back unused high ram.
    u32 giveback = ALIGN_DOWN(ZoneHigh.cur - ZoneHigh.bottom, PAGE_SIZE);
    add_e820(ZoneHigh.bottom, giveback, E820_RAM);
    dprintf(1, "Returned %d bytes of ZoneHigh\n", giveback);

    // Clear low-memory allocations.
    memset((void*)ZoneTmpLow.bottom, 0, ZoneTmpLow.top - ZoneTmpLow.bottom);
}


/****************************************************************
 * pmm interface
 ****************************************************************/

struct pmmheader {
    u32 signature;
    u8 version;
    u8 length;
    u8 checksum;
    u16 entry_offset;
    u16 entry_seg;
    u8 reserved[5];
} PACKED;

extern struct pmmheader PMMHEADER;

#define PMM_SIGNATURE 0x4d4d5024 // $PMM

#if CONFIG_PMM
struct pmmheader PMMHEADER __aligned(16) VAR16EXPORT = {
    .version = 0x01,
    .length = sizeof(PMMHEADER),
    .entry_seg = SEG_BIOS,
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

u32 VISIBLE16
handle_pmm(u16 *args)
{
    if (! CONFIG_PMM)
        return PMM_FUNCTION_NOT_SUPPORTED;

    u16 arg1 = args[0];
    dprintf(DEBUG_HDL_pmm, "pmm call arg1=%x\n", arg1);

    switch (arg1) {
    case 0x00: return handle_pmm00(args);
    case 0x01: return handle_pmm01(args);
    case 0x02: return handle_pmm02(args);
    default:   return handle_pmmXX(args);
    }
}

// romlayout.S
extern void entry_pmm(void);

void
pmm_setup(void)
{
    if (! CONFIG_PMM)
        return;

    dprintf(3, "init PMM\n");

    PMMHEADER.signature = PMM_SIGNATURE;
    PMMHEADER.entry_offset = (u32)entry_pmm - BUILD_BIOS_ADDR;
    PMMHEADER.checksum -= checksum(&PMMHEADER, sizeof(PMMHEADER));
}

void
pmm_finalize(void)
{
    if (! CONFIG_PMM)
        return;

    dprintf(3, "finalize PMM\n");

    PMMHEADER.signature = 0;
    PMMHEADER.entry_offset = 0;
}
