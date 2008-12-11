// Code for handling calls to "post" that are resume related.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "util.h" // dprintf
#include "ioport.h" // outb
#include "pic.h" // eoi_pic2
#include "biosvar.h" // struct bios_data_area_s

// Reset DMA controller
void
init_dma()
{
    // first reset the DMA controllers
    outb(0, PORT_DMA1_MASTER_CLEAR);
    outb(0, PORT_DMA2_MASTER_CLEAR);

    // then initialize the DMA controllers
    outb(0xc0, PORT_DMA2_MODE_REG);
    outb(0x00, PORT_DMA2_MASK_REG);
}

// Handler for post calls that look like a resume.
void VISIBLE16
handle_resume(u8 status)
{
    init_dma();

    debug_serial_setup();
    dprintf(1, "In resume (status=%d)\n", status);

    switch (status) {
    case 0x00:
    case 0x09:
    case 0x0d ... 0xff:
        // Normal post - now that status has been cleared a reset will
        // run regular boot code..
        reset_vector();
        break;

    case 0x05:
        // flush keyboard (issue EOI) and jump via 40h:0067h
        eoi_pic2();
        // NO BREAK
    case 0x0a:
        // resume execution by jump via 40h:0067h
#define bda ((struct bios_data_area_s *)0)
        asm volatile(
            "movw %%ax, %%ds\n"
            "ljmpw *%0\n"
            : : "m"(bda->jump_ip), "a"(SEG_BDA)
            );
        break;

    case 0x0b:
        // resume execution via IRET via 40h:0067h
        asm volatile(
            "movw %%ax, %%ds\n"
            "movw %0, %%sp\n"
            "movw %1, %%ss\n"
            "iretw\n"
            : : "m"(bda->jump_ip), "m"(bda->jump_cs), "a"(SEG_BDA)
            );
        break;

    case 0x0c:
        // resume execution via RETF via 40h:0067h
        asm volatile(
            "movw %%ax, %%ds\n"
            "movw %0, %%sp\n"
            "movw %1, %%ss\n"
            "lretw\n"
            : : "m"(bda->jump_ip), "m"(bda->jump_cs), "a"(SEG_BDA)
            );
        break;
    }

    BX_PANIC("Unimplemented shutdown status: %02x\n", status);
}
