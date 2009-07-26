// Post memory manager (PMM) calls
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // checksum
#include "config.h" // BUILD_BIOS_ADDR

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

#define FUNCTION_NOT_SUPPORTED 0xffffffff

// PMM - allocate
static u32
handle_pmm00(u16 *args)
{
    u32 length = *(u32*)&args[1], handle = *(u32*)&args[3];
    u16 flags = args[5];
    dprintf(1, "pmm00: length=%x handle=%x flags=%x\n"
            , length, handle, flags);
    // XXX
    return 0;
}

// PMM - find
static u32
handle_pmm01(u16 *args)
{
    u32 handle = *(u32*)&args[1];
    dprintf(1, "pmm01: handle=%x\n", handle);
    // XXX
    return 0;
}

// PMM - deallocate
static u32
handle_pmm02(u16 *args)
{
    u32 buffer = *(u32*)&args[1];
    dprintf(1, "pmm02: buffer=%x\n", buffer);
    // XXX
    return 0;
}

static u32
handle_pmmXX(u16 *args)
{
    return FUNCTION_NOT_SUPPORTED;
}

u32 VISIBLE16
handle_pmm(u16 *args)
{
    if (! CONFIG_PMM)
        return FUNCTION_NOT_SUPPORTED;

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

    // XXX - zero low-memory allocations.
}
