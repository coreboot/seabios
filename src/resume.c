// Code for handling calls to "post" that are resume related.
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "ioport.h" // outb
#include "pic.h" // eoi_pic2
#include "biosvar.h" // struct bios_data_area_s
#include "bregs.h" // struct bregs
#include "acpi.h" // find_resume_vector

// Reset DMA controller
void
init_dma(void)
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
    case 0xfe:
        if (CONFIG_S3_RESUME) {
            // S3 resume request.  Jump to 32bit mode to handle the resume.
            asm volatile(
                "movw %w1, %%ss\n"
                "movl %0, %%esp\n"
                "movl $_cfunc32flat_s3_resume, %%edx\n"
                "jmp transition32\n"
                : : "i"(BUILD_S3RESUME_STACK_ADDR), "r"(0)
                );
            break;
        }
        // NO BREAK
    case 0x00:
    case 0x0d ... 0xfd:
    case 0xff:
        // Normal post - now that status has been cleared a reset will
        // run regular boot code..
        reset_vector();
        break;

    case 0x05:
        // flush keyboard (issue EOI) and jump via 40h:0067h
        eoi_pic2();
        // NO BREAK
    case 0x0a:
#define BDA_JUMP (((struct bios_data_area_s *)0)->jump)
        // resume execution by jump via 40h:0067h
        asm volatile(
            "movw %w1, %%ds\n"
            "ljmpw *%0\n"
            : : "m"(BDA_JUMP), "r"(SEG_BDA)
            );
        break;

    case 0x0b:
        // resume execution via IRET via 40h:0067h
        asm volatile(
            "movw %w1, %%ds\n"
            "lssw %0, %%sp\n"
            "iretw\n"
            : : "m"(BDA_JUMP), "r"(SEG_BDA)
            );
        break;

    case 0x0c:
        // resume execution via RETF via 40h:0067h
        asm volatile(
            "movw %w1, %%ds\n"
            "lssw %0, %%sp\n"
            "lretw\n"
            : : "m"(BDA_JUMP), "r"(SEG_BDA)
            );
        break;
    }

    panic("Unimplemented shutdown status: %02x\n", status);
}

void VISIBLE32FLAT
s3_resume(void)
{
    ASSERT32FLAT();
    if (!CONFIG_S3_RESUME)
        panic("S3 resume support not compiled in.\n");

    dprintf(1, "In 32bit resume\n");

    smm_init();

    s3_resume_vga_init();

    make_bios_readonly();

    u32 s3_resume_vector = find_resume_vector();

    // Invoke the resume vector.
    struct bregs br;
    memset(&br, 0, sizeof(br));
    if (s3_resume_vector) {
        dprintf(1, "Jump to resume vector (%x)\n", s3_resume_vector);
        br.code = FLATPTR_TO_SEGOFF((void*)s3_resume_vector);
    } else {
        dprintf(1, "No resume vector set!\n");
        // Jump to the post vector to restart with a normal boot.
        br.code = SEGOFF(SEG_BIOS, (u32)reset_vector - BUILD_BIOS_ADDR);
    }
    call16big(&br);
}
