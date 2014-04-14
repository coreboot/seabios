// Main VGA bios initialization
//
// Copyright (C) 2009-2013  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2001-2008 the LGPL VGABios developers Team
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "biosvar.h" // SET_BDA
#include "bregs.h" // struct bregs
#include "hw/pci.h" // pci_config_readw
#include "hw/pci_regs.h" // PCI_VENDOR_ID
#include "hw/serialio.h" // serial_debug_preinit
#include "output.h" // dprintf
#include "std/optionrom.h" // struct pci_data
#include "std/pmm.h" // struct pmmheader
#include "string.h" // checksum_far
#include "util.h" // VERSION
#include "vgabios.h" // struct VideoSavePointer_s
#include "vgahw.h" // vgahw_setup

// Standard Video Save Pointer Table
struct VideoSavePointer_s {
    struct segoff_s videoparam;
    struct segoff_s paramdynamicsave;
    struct segoff_s textcharset;
    struct segoff_s graphcharset;
    struct segoff_s secsavepointer;
    u8 reserved[8];
} PACKED;

struct VideoSavePointer_s video_save_pointer_table VAR16;

struct VideoParam_s video_param_table[29] VAR16;

// Type of emulator platform - for dprintf with certain compile options.
int PlatformRunningOn VAR16;


/****************************************************************
 * PCI Data
 ****************************************************************/

struct pci_data rom_pci_data VAR16 VISIBLE16 = {
    .signature = PCI_ROM_SIGNATURE,
    .vendor = CONFIG_VGA_VID,
    .device = CONFIG_VGA_DID,
    .dlen = 0x18,
    .class_hi = 0x300,
    .irevision = 1,
    .type = PCIROM_CODETYPE_X86,
    .indicator = 0x80,
};


/****************************************************************
 * PMM call and extra stack setup
 ****************************************************************/

u16 ExtraStackSeg VAR16 VISIBLE16;

static void
allocate_extra_stack(void)
{
    if (!CONFIG_VGA_ALLOCATE_EXTRA_STACK)
        return;
    u32 pmmscan;
    for (pmmscan=0; pmmscan < BUILD_BIOS_SIZE; pmmscan+=16) {
        struct pmmheader *pmm = (void*)pmmscan;
        if (GET_FARVAR(SEG_BIOS, pmm->signature) != PMM_SIGNATURE)
            continue;
        if (checksum_far(SEG_BIOS, pmm, GET_FARVAR(SEG_BIOS, pmm->length)))
            continue;
        struct segoff_s entry = GET_FARVAR(SEG_BIOS, pmm->entry);
        dprintf(1, "Attempting to allocate VGA stack via pmm call to %04x:%04x\n"
                , entry.seg, entry.offset);
        u16 res1, res2;
        asm volatile(
            "pushl %0\n"
            "pushw $(8|1)\n"            // Permanent low memory request
            "pushl $0xffffffff\n"       // Anonymous handle
            "pushl $" __stringify(CONFIG_VGA_EXTRA_STACK_SIZE/16) "\n"
            "pushw $0x00\n"             // PMM allocation request
            "lcallw *12(%%esp)\n"
            "addl $16, %%esp\n"
            "cli\n"
            "cld\n"
            : "+r" (entry.segoff), "=a" (res1), "=d" (res2) : : "cc", "memory");
        u32 res = res1 | (res2 << 16);
        if (!res || res == PMM_FUNCTION_NOT_SUPPORTED)
            return;
        dprintf(1, "VGA stack allocated at %x\n", res);
        SET_VGA(ExtraStackSeg, res >> 4);
        extern void entry_10_extrastack(void);
        SET_IVT(0x10, SEGOFF(get_global_seg(), (u32)entry_10_extrastack));
        return;
    }
}


/****************************************************************
 * VGA post
 ****************************************************************/

static void
init_bios_area(void)
{
    // init detected hardware BIOS Area
    // set 80x25 color (not clear from RBIL but usual)
    set_equipment_flags(0x30, 0x20);

    // the default char height
    SET_BDA(char_height, 0x10);

    // Clear the screen
    SET_BDA(video_ctl, 0x60);

    // Set the basic screen we have
    SET_BDA(video_switches, 0xf9);

    // Set the basic modeset options
    SET_BDA(modeset_ctl, 0x51);

    // Set the  default MSR
    SET_BDA(video_msr, 0x09);
}

int VgaBDF VAR16 = -1;
int HaveRunInit VAR16;

void VISIBLE16
vga_post(struct bregs *regs)
{
    serial_debug_preinit();
    dprintf(1, "Start SeaVGABIOS (version %s)\n", VERSION);
    debug_enter(regs, DEBUG_VGA_POST);

    if (CONFIG_VGA_PCI && !GET_GLOBAL(HaveRunInit)) {
        u16 bdf = regs->ax;
        if ((pci_config_readw(bdf, PCI_VENDOR_ID)
             == GET_GLOBAL(rom_pci_data.vendor))
            && (pci_config_readw(bdf, PCI_DEVICE_ID)
                == GET_GLOBAL(rom_pci_data.device)))
            SET_VGA(VgaBDF, bdf);
    }

    int ret = vgahw_setup();
    if (ret) {
        dprintf(1, "Failed to initialize VGA hardware.  Exiting.\n");
        return;
    }

    if (GET_GLOBAL(HaveRunInit))
        return;

    init_bios_area();

    SET_VGA(video_save_pointer_table.videoparam
            , SEGOFF(get_global_seg(), (u32)video_param_table));
    if (CONFIG_VGA_STDVGA_PORTS)
        stdvga_build_video_param();

    extern void entry_10(void);
    SET_IVT(0x10, SEGOFF(get_global_seg(), (u32)entry_10));

    allocate_extra_stack();

    SET_VGA(HaveRunInit, 1);

    // Fixup checksum
    extern u8 _rom_header_size, _rom_header_checksum;
    SET_VGA(_rom_header_checksum, 0);
    u8 sum = -checksum_far(get_global_seg(), 0,
                           GET_GLOBAL(_rom_header_size) * 512);
    SET_VGA(_rom_header_checksum, sum);
}
