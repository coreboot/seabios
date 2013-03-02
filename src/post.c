// 32bit code to Power On Self Test (POST) a machine.
//
// Copyright (C) 2008-2013  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "config.h" // CONFIG_*
#include "cmos.h" // CMOS_*
#include "util.h" // memset
#include "biosvar.h" // struct bios_data_area_s
#include "disk.h" // floppy_setup
#include "ata.h" // ata_setup
#include "ahci.h" // ahci_setup
#include "memmap.h" // add_e820
#include "pic.h" // pic_setup
#include "bregs.h" // struct bregs
#include "boot.h" // boot_init
#include "usb.h" // usb_setup
#include "paravirt.h" // qemu_cfg_preinit
#include "xen.h" // xen_preinit
#include "ps2port.h" // ps2port_setup
#include "virtio-blk.h" // virtio_blk_setup
#include "virtio-scsi.h" // virtio_scsi_setup
#include "lsi-scsi.h" // lsi_scsi_setup
#include "esp-scsi.h" // esp_scsi_setup
#include "megasas.h" // megasas_setup
#include "post.h" // interface_init


/****************************************************************
 * BIOS initialization and hardware setup
 ****************************************************************/

static void
ivt_init(void)
{
    dprintf(3, "init ivt\n");

    // Setup reset-vector entry point (controls legacy reboots).
    HaveRunPost = 1;
    outb_cmos(0, CMOS_RESET_CODE);

    // Initialize all vectors to the default handler.
    int i;
    for (i=0; i<256; i++)
        SET_IVT(i, FUNC16(entry_iret_official));

    // Initialize all hw vectors to a default hw handler.
    for (i=BIOS_HWIRQ0_VECTOR; i<BIOS_HWIRQ0_VECTOR+8; i++)
        SET_IVT(i, FUNC16(entry_hwpic1));
    for (i=BIOS_HWIRQ8_VECTOR; i<BIOS_HWIRQ8_VECTOR+8; i++)
        SET_IVT(i, FUNC16(entry_hwpic2));

    // Initialize software handlers.
    SET_IVT(0x02, FUNC16(entry_02));
    SET_IVT(0x10, FUNC16(entry_10));
    SET_IVT(0x11, FUNC16(entry_11));
    SET_IVT(0x12, FUNC16(entry_12));
    SET_IVT(0x13, FUNC16(entry_13_official));
    SET_IVT(0x14, FUNC16(entry_14));
    SET_IVT(0x15, FUNC16(entry_15));
    SET_IVT(0x16, FUNC16(entry_16));
    SET_IVT(0x17, FUNC16(entry_17));
    SET_IVT(0x18, FUNC16(entry_18));
    SET_IVT(0x19, FUNC16(entry_19_official));
    SET_IVT(0x1a, FUNC16(entry_1a_official));
    SET_IVT(0x40, FUNC16(entry_40));

    // INT 60h-66h reserved for user interrupt
    for (i=0x60; i<=0x66; i++)
        SET_IVT(i, SEGOFF(0, 0));

    // set vector 0x79 to zero
    // this is used by 'gardian angel' protection system
    SET_IVT(0x79, SEGOFF(0, 0));
}

static void
bda_init(void)
{
    dprintf(3, "init bda\n");

    struct bios_data_area_s *bda = MAKE_FLATPTR(SEG_BDA, 0);
    memset(bda, 0, sizeof(*bda));

    int esize = EBDA_SIZE_START;
    SET_BDA(mem_size_kb, BUILD_LOWRAM_END/1024 - esize);
    u16 ebda_seg = EBDA_SEGMENT_START;
    SET_BDA(ebda_seg, ebda_seg);

    // Init ebda
    struct extended_bios_data_area_s *ebda = get_ebda_ptr();
    memset(ebda, 0, sizeof(*ebda));
    ebda->size = esize;

    add_e820((u32)MAKE_FLATPTR(ebda_seg, 0), GET_EBDA(ebda_seg, size) * 1024
             , E820_RESERVED);

    // Init extra stack
    StackPos = (void*)(&ExtraStack[BUILD_EXTRA_STACK_SIZE] - zonelow_base);
}

void
interface_init(void)
{
    // Running at new code address - do code relocation fixups
    malloc_init();

    // Setup romfile items.
    qemu_cfg_init();
    coreboot_cbfs_init();

    // Setup ivt/bda/ebda
    ivt_init();
    bda_init();

    // Other interfaces
    boot_init();
    bios32_init();
    pmm_init();
    pnp_init();
    kbd_init();
    mouse_init();
}

// Initialize hardware devices
void
device_hardware_setup(void)
{
    usb_setup();
    ps2port_setup();
    lpt_setup();
    serial_setup();

    floppy_setup();
    ata_setup();
    ahci_setup();
    cbfs_payload_setup();
    ramdisk_setup();
    virtio_blk_setup();
    virtio_scsi_setup();
    lsi_scsi_setup();
    esp_scsi_setup();
    megasas_setup();
}

static void
platform_hardware_setup(void)
{
    // Enable CPU caching
    setcr0(getcr0() & ~(CR0_CD|CR0_NW));

    // Make sure legacy DMA isn't running.
    dma_setup();

    // Init base pc hardware.
    pic_setup();
    mathcp_setup();
    timer_setup();

    // Platform specific setup
    qemu_platform_setup();
    coreboot_platform_setup();
}

void
prepareboot(void)
{
    // Run BCVs
    bcv_prepboot();

    // Finalize data structures before boot
    cdrom_prepboot();
    pmm_prepboot();
    malloc_prepboot();
    memmap_prepboot();

    // Setup bios checksum.
    BiosChecksum -= checksum((u8*)BUILD_BIOS_ADDR, BUILD_BIOS_SIZE);
}

// Begin the boot process by invoking an int0x19 in 16bit mode.
void VISIBLE32FLAT
startBoot(void)
{
    // Clear low-memory allocations (required by PMM spec).
    memset((void*)BUILD_STACK_ADDR, 0, BUILD_EBDA_MINIMUM - BUILD_STACK_ADDR);

    dprintf(3, "Jump to int19\n");
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.flags = F_IF;
    call16_int(0x19, &br);
}

// Main setup code.
static void
maininit(void)
{
    // Initialize internal interfaces.
    interface_init();

    // Setup platform devices.
    platform_hardware_setup();

    // Start hardware initialization (if optionrom threading)
    if (CONFIG_THREAD_OPTIONROMS)
        device_hardware_setup();

    // Run vga option rom
    vgarom_setup();

    // Do hardware initialization (if running synchronously)
    if (!CONFIG_THREAD_OPTIONROMS) {
        device_hardware_setup();
        wait_threads();
    }

    // Run option roms
    optionrom_setup();

    // Allow user to modify overall boot order.
    interactive_bootmenu();
    wait_threads();

    // Prepare for boot.
    prepareboot();

    // Write protect bios memory.
    make_bios_readonly();

    // Invoke int 19 to start boot process.
    startBoot();
}


/****************************************************************
 * POST entry and code relocation
 ****************************************************************/

// Update given relocs for the code at 'dest' with a given 'delta'
static void
updateRelocs(void *dest, u32 *rstart, u32 *rend, u32 delta)
{
    u32 *reloc;
    for (reloc = rstart; reloc < rend; reloc++)
        *((u32*)(dest + *reloc)) += delta;
}

// Relocate init code and then call a function at its new address.
// The passed function should be in the "init" section and must not
// return.
void __noreturn
reloc_preinit(void *f, void *arg)
{
    void (*func)(void *) __noreturn = f;
    if (!CONFIG_RELOCATE_INIT)
        func(arg);
    // Symbols populated by the build.
    extern u8 code32flat_start[];
    extern u8 _reloc_min_align;
    extern u32 _reloc_abs_start[], _reloc_abs_end[];
    extern u32 _reloc_rel_start[], _reloc_rel_end[];
    extern u32 _reloc_init_start[], _reloc_init_end[];
    extern u8 code32init_start[], code32init_end[];

    // Allocate space for init code.
    u32 initsize = code32init_end - code32init_start;
    u32 codealign = (u32)&_reloc_min_align;
    void *codedest = memalign_tmp(codealign, initsize);
    if (!codedest)
        panic("No space for init relocation.\n");

    // Copy code and update relocs (init absolute, init relative, and runtime)
    dprintf(1, "Relocating init from %p to %p (size %d)\n"
            , code32init_start, codedest, initsize);
    s32 delta = codedest - (void*)code32init_start;
    memcpy(codedest, code32init_start, initsize);
    updateRelocs(codedest, _reloc_abs_start, _reloc_abs_end, delta);
    updateRelocs(codedest, _reloc_rel_start, _reloc_rel_end, -delta);
    updateRelocs(code32flat_start, _reloc_init_start, _reloc_init_end, delta);
    if (f >= (void*)code32init_start && f < (void*)code32init_end)
        func = f + delta;

    // Call function in relocated code.
    barrier();
    func(arg);
}

// Setup for code relocation and then relocate.
void VISIBLE32INIT
dopost(void)
{
    // Detect ram and setup internal malloc.
    qemu_preinit();
    coreboot_preinit();
    malloc_preinit();

    // Relocate initialization code and call maininit().
    reloc_preinit(maininit, NULL);
}

// Entry point for Power On Self Test (POST) - the BIOS initilization
// phase.  This function makes the memory at 0xc0000-0xfffff
// read/writable and then calls dopost().
void VISIBLE32FLAT
handle_post(void)
{
    if (!CONFIG_QEMU && !CONFIG_COREBOOT)
        return;

    debug_serial_preinit();
    dprintf(1, "Start bios (version %s)\n", VERSION);

    // Check if we are running under Xen.
    xen_preinit();

    // Allow writes to modify bios area (0xf0000)
    make_bios_writable();

    // Now that memory is read/writable - start post process.
    dopost();
}
