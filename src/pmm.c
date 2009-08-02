// Post memory manager (PMM) calls
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // checksum
#include "config.h" // BUILD_BIOS_ADDR
#include "memmap.h" // e820_list
#include "farptr.h" // GET_FARVAR
#include "biosvar.h" // EBDA_SEGMENT_MINIMUM


/****************************************************************
 * malloc
 ****************************************************************/

#if MODE16
// The 16bit pmm entry points runs in "big real" mode, and can
// therefore read/write to the 32bit malloc variables.
#define GET_PMMVAR(var) GET_FARVAR(0, (var))
#define SET_PMMVAR(var, val) SET_FARVAR(0, (var), (val))
#else
#define GET_PMMVAR(var) (var)
#define SET_PMMVAR(var, val) do { (var) = (val); } while (0)
#endif

// Zone definitions
struct zone_s {
    u32 top, bottom, cur;
};

struct zone_s ZoneHigh VAR32VISIBLE, ZoneFSeg VAR32VISIBLE;
struct zone_s ZoneTmpLow VAR32VISIBLE, ZoneTmpHigh VAR32VISIBLE;

struct zone_s *Zones[] VAR32VISIBLE = {
    &ZoneTmpLow, &ZoneFSeg, &ZoneTmpHigh, &ZoneHigh
};

// Obtain memory from a given zone.
static void *
zone_malloc(struct zone_s *zone, u32 size, u32 align)
{
    u32 newpos = (GET_PMMVAR(zone->cur) - size) / align * align;
    if ((s32)(newpos - GET_PMMVAR(zone->bottom)) < 0)
        // No space
        return NULL;
    SET_PMMVAR(zone->cur, newpos);
    return (void*)newpos;
}

// Return memory to a zone (if it was the last to be allocated).
static void
zone_free(struct zone_s *zone, void *data, u32 olddata)
{
    if (! data || GET_PMMVAR(zone->cur) != (u32)data)
        return;
    SET_PMMVAR(zone->cur, olddata);
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

// Report the status of all the zones.
static void
dumpZones()
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

// Allocate memory at the top of 32bit ram.
void *
malloc_high(u32 size)
{
    return zone_malloc(&ZoneHigh, size, MALLOC_MIN_ALIGN);
}

// Allocate memory in the 0xf0000-0x100000 area of ram.
void *
malloc_fseg(u32 size)
{
    return zone_malloc(&ZoneFSeg, size, MALLOC_MIN_ALIGN);
}

void
malloc_setup()
{
    ASSERT32();
    dprintf(3, "malloc setup\n");

    // Memory in 0xf0000 area.
    memset(BiosTableSpace, 0, CONFIG_MAX_BIOSTABLE);
    ZoneFSeg.bottom = (u32)BiosTableSpace;
    ZoneFSeg.top = ZoneFSeg.cur = ZoneFSeg.bottom + CONFIG_MAX_BIOSTABLE;

    // Memory under 1Meg.
    ZoneTmpLow.bottom = BUILD_STACK_ADDR;
    ZoneTmpLow.top = ZoneTmpLow.cur = (u32)MAKE_FLATPTR(EBDA_SEGMENT_MINIMUM, 0);

    // Find memory at the top of ram.
    u32 top = 0, bottom = 0;
    int i;
    for (i=e820_count-1; i>=0; i--) {
        struct e820entry *e = &e820_list[i];
        u64 end = e->start + e->size;
        if (e->type != E820_RAM || end > 0xffffffff
            || e->size < CONFIG_MAX_HIGHTABLE + MALLOC_MIN_ALIGN)
            continue;
        top = end;
        bottom = e->start;
        break;
    }
    if (top < 1024*1024 + CONFIG_MAX_HIGHTABLE) {
        // No memory above 1Meg
        memset(&ZoneHigh, 0, sizeof(ZoneHigh));
        memset(&ZoneTmpHigh, 0, sizeof(ZoneHigh));
        return;
    }

    // Memory at top of ram.
    ZoneHigh.bottom = ALIGN(top - CONFIG_MAX_HIGHTABLE, MALLOC_MIN_ALIGN);
    ZoneHigh.top = ZoneHigh.cur = ZoneHigh.bottom + CONFIG_MAX_HIGHTABLE;
    add_e820(ZoneHigh.bottom, CONFIG_MAX_HIGHTABLE, E820_RESERVED);

    // Memory above 1Meg
    ZoneTmpHigh.bottom = ALIGN(bottom, MALLOC_MIN_ALIGN);
    ZoneTmpHigh.top = ZoneTmpHigh.cur = ZoneHigh.bottom;
}

void
malloc_finalize()
{
    dprintf(3, "malloc finalize\n");

    dumpZones();

    // Give back unused high ram.
    u32 giveback = (ZoneHigh.cur - ZoneHigh.bottom) / 4096 * 4096;
    add_e820(ZoneHigh.bottom, giveback, E820_RAM);
    dprintf(1, "Returned %d bytes of ZoneHigh\n", giveback);

    // Clear low-memory allocations.
    memset((void*)ZoneTmpLow.bottom, 0, ZoneTmpLow.top - ZoneTmpLow.bottom);
}


/****************************************************************
 * pmm allocation
 ****************************************************************/

// Information on PMM tracked allocations
struct pmmalloc_s {
    void *data;
    u32 olddata;
    u32 handle;
    u32 oldallocdata;
    struct pmmalloc_s *next;
};

struct pmmalloc_s *PMMAllocs VAR32VISIBLE;

// Memory zone that pmm allocation tracking info is stored in
#define ZONEALLOC (&ZoneTmpHigh)

// Allocate memory from the given zone and track it as a PMM allocation
static void *
pmm_malloc(struct zone_s *zone, u32 handle, u32 size, u32 align)
{
    u32 oldallocdata = GET_PMMVAR(ZONEALLOC->cur);
    struct pmmalloc_s *info = zone_malloc(ZONEALLOC, sizeof(*info)
                                          , MALLOC_MIN_ALIGN);
    if (!info)
        return NULL;
    u32 olddata = GET_PMMVAR(zone->cur);
    void *data = zone_malloc(zone, size, align);
    if (! data) {
        zone_free(ZONEALLOC, info, oldallocdata);
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
pmm_free_data(struct zone_s *zone, void *data, u32 olddata)
{
    if (GET_PMMVAR(zone->cur) == (u32)data) {
        zone_free(zone, data, olddata);
        return;
    }
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
static int
pmm_free(void *data)
{
    struct zone_s *zone = zone_find(GET_PMMVAR(data));
    if (!zone)
        return -1;
    struct pmmalloc_s **pinfo = &PMMAllocs;
    for (;;) {
        struct pmmalloc_s *info = GET_PMMVAR(*pinfo);
        if (!info)
            return -1;
        if (GET_PMMVAR(info->data) == data) {
            SET_PMMVAR(*pinfo, GET_PMMVAR(info->next));
            u32 oldallocdata = GET_PMMVAR(info->oldallocdata);
            u32 olddata = GET_PMMVAR(info->olddata);
            pmm_free_data(zone, data, olddata);
            pmm_free_data(ZONEALLOC, info, oldallocdata);
            dprintf(8, "pmm_free data=%p zone=%p olddata=%p oldallocdata=%p"
                    " info=%p\n"
                    , data, zone, (void*)olddata, (void*)oldallocdata
                    , info);
            return 0;
        }
        pinfo = &info->next;
    }
}

// Find the amount of free space in a given zone.
static u32
pmm_getspace(struct zone_s *zone)
{
    u32 space = GET_PMMVAR(zone->cur) - GET_PMMVAR(zone->bottom);
    if (zone != ZONEALLOC)
        return space;
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
    if (!length) {
        // Memory size request
        switch (flags & 3) {
        default:
        case 0:
            return 0;
        case 1:
            return pmm_getspace(&ZoneTmpLow);
        case 2:
            return pmm_getspace(&ZoneTmpHigh);
        case 3: {
            u32 spacelow = pmm_getspace(&ZoneTmpLow);
            u32 spacehigh = pmm_getspace(&ZoneTmpHigh);
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
        return (u32)pmm_malloc(&ZoneTmpLow, handle, size, align);
    case 2:
        return (u32)pmm_malloc(&ZoneTmpHigh, handle, size, align);
    case 3: {
        void *data = pmm_malloc(&ZoneTmpLow, handle, size, align);
        if (data)
            return (u32)data;
        return (u32)pmm_malloc(&ZoneTmpHigh, handle, size, align);
    }
    }
}

// PMM - find
static u32
handle_pmm01(u16 *args)
{
    u32 handle = *(u32*)&args[1];
    dprintf(3, "pmm01: handle=%x\n", handle);
    if (handle == 0xFFFFFFFF)
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
extern void entry_pmm();

void
pmm_setup()
{
    if (! CONFIG_PMM)
        return;

    dprintf(3, "init PMM\n");

    PMMAllocs = NULL;

    PMMHEADER.signature = PMM_SIGNATURE;
    PMMHEADER.entry_offset = (u32)entry_pmm - BUILD_BIOS_ADDR;
    PMMHEADER.checksum -= checksum(&PMMHEADER, sizeof(PMMHEADER));
}

void
pmm_finalize()
{
    if (! CONFIG_PMM)
        return;

    dprintf(3, "finalize PMM\n");

    PMMHEADER.signature = 0;
    PMMHEADER.entry_offset = 0;
}
