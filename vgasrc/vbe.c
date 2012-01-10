// Video Bios Extensions handlers
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2001-2008 the LGPL VGABios developers Team
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "vgabios.h" // handle_104f
#include "config.h" // CONFIG_*
#include "bregs.h" // struct bregs
#include "vbe.h" // struct vbe_info
#include "util.h" // dprintf
#include "biosvar.h" // get_global_set
#include "bochsvga.h" // bochsvga_hires_enabled
#include "vgahw.h" // vgahw_set_mode

static void
vbe_104f00(struct bregs *regs)
{
    u16 seg = regs->es;
    struct vbe_info *info = (void*)(regs->di+0);

    if (GET_FARVAR(seg, info->signature) == VBE2_SIGNATURE) {
        dprintf(4, "Get VBE Controller: VBE2 Signature found\n");
    } else if (GET_FARVAR(seg, info->signature) == VESA_SIGNATURE) {
        dprintf(4, "Get VBE Controller: VESA Signature found\n");
    } else {
        dprintf(4, "Get VBE Controller: Invalid Signature\n");
    }

    memset_far(seg, info, 0, sizeof(*info));

    SET_FARVAR(seg, info->signature, VESA_SIGNATURE);

    SET_FARVAR(seg, info->version, 0x0200);

    SET_FARVAR(seg, info->oem_string,
            SEGOFF(get_global_seg(), (u32)VBE_OEM_STRING));
    SET_FARVAR(seg, info->capabilities, 0x1); /* 8BIT DAC */

    /* We generate our mode list in the reserved field of the info block */
    u16 *destmode = (void*)info->reserved;
    SET_FARVAR(seg, info->video_mode, SEGOFF(seg, (u32)destmode));

    /* Total memory (in 64 blocks) */
    SET_FARVAR(seg, info->total_memory, bochsvga_total_mem());

    SET_FARVAR(seg, info->oem_vendor_string,
            SEGOFF(get_global_seg(), (u32)VBE_VENDOR_STRING));
    SET_FARVAR(seg, info->oem_product_string,
            SEGOFF(get_global_seg(), (u32)VBE_PRODUCT_STRING));
    SET_FARVAR(seg, info->oem_revision_string,
            SEGOFF(get_global_seg(), (u32)VBE_REVISION_STRING));

    /* Fill list of modes */
    u16 *last = (void*)&info->reserved[sizeof(info->reserved)];
    vgahw_list_modes(seg, destmode, last - 1);

    regs->al = regs->ah; /* 0x4F, Function supported */
    regs->ah = 0x0; /* 0x0, Function call successful */
}

static void
vbe_104f01(struct bregs *regs)
{
    u16 seg = regs->es;
    struct vbe_mode_info *info = (void*)(regs->di+0);
    u16 mode = regs->cx;
    struct vbe_modeinfo modeinfo;
    int rc;

    dprintf(1, "VBE mode info request: %x\n", mode);

    rc = bochsvga_mode_info(mode, &modeinfo);
    if (rc) {
        dprintf(1, "VBE mode %x not found\n", mode);
        regs->ax = 0x100;
        return;
    }

    u16 mode_attr = VBE_MODE_ATTRIBUTE_SUPPORTED |
                    VBE_MODE_ATTRIBUTE_EXTENDED_INFORMATION_AVAILABLE |
                    VBE_MODE_ATTRIBUTE_COLOR_MODE |
                    VBE_MODE_ATTRIBUTE_GRAPHICS_MODE;
    if (modeinfo.depth == 4)
        mode_attr |= VBE_MODE_ATTRIBUTE_TTY_BIOS_SUPPORT;
    else
        mode_attr |= VBE_MODE_ATTRIBUTE_LINEAR_FRAME_BUFFER_MODE;
    SET_FARVAR(seg, info->mode_attributes, mode_attr);
    SET_FARVAR(seg, info->winA_attributes,
               VBE_WINDOW_ATTRIBUTE_RELOCATABLE |
               VBE_WINDOW_ATTRIBUTE_READABLE |
               VBE_WINDOW_ATTRIBUTE_WRITEABLE);
    SET_FARVAR(seg, info->winB_attributes, 0);
    SET_FARVAR(seg, info->win_granularity, 64); /* Bank size 64K */
    SET_FARVAR(seg, info->win_size, 64); /* Bank size 64K */
    SET_FARVAR(seg, info->winA_seg, 0xA000);
    SET_FARVAR(seg, info->winB_seg, 0x0);
    SET_FARVAR(seg, info->win_func_ptr.segoff, 0x0);
    SET_FARVAR(seg, info->bytes_per_scanline, modeinfo.linesize);
    SET_FARVAR(seg, info->xres, modeinfo.width);
    SET_FARVAR(seg, info->yres, modeinfo.height);
    SET_FARVAR(seg, info->xcharsize, 8);
    SET_FARVAR(seg, info->ycharsize, 16);
    if (modeinfo.depth == 4)
        SET_FARVAR(seg, info->planes, 4);
    else
        SET_FARVAR(seg, info->planes, 1);
    SET_FARVAR(seg, info->bits_per_pixel, modeinfo.depth);
    SET_FARVAR(seg, info->banks,
            (modeinfo.linesize * modeinfo.height + 65535) / 65536);
    if (modeinfo.depth == 4)
        SET_FARVAR(seg, info->mem_model, VBE_MEMORYMODEL_PLANAR);
    else if (modeinfo.depth == 8)
        SET_FARVAR(seg, info->mem_model, VBE_MEMORYMODEL_PACKED_PIXEL);
    else
        SET_FARVAR(seg, info->mem_model, VBE_MEMORYMODEL_DIRECT_COLOR);
    SET_FARVAR(seg, info->bank_size, 0);
    u32 pages = modeinfo.vram_size / (modeinfo.height * modeinfo.linesize);
    if (modeinfo.depth == 4)
        SET_FARVAR(seg, info->pages, (pages / 4) - 1);
    else
        SET_FARVAR(seg, info->pages, pages - 1);
    SET_FARVAR(seg, info->reserved0, 1);

    u8 r_size, r_pos, g_size, g_pos, b_size, b_pos, a_size, a_pos;

    switch (modeinfo.depth) {
    case 15: r_size = 5; r_pos = 10; g_size = 5; g_pos = 5;
             b_size = 5; b_pos = 0; a_size = 1; a_pos = 15; break;
    case 16: r_size = 5; r_pos = 11; g_size = 6; g_pos = 5;
             b_size = 5; b_pos = 0; a_size = 0; a_pos = 0; break;
    case 24: r_size = 8; r_pos = 16; g_size = 8; g_pos = 8;
             b_size = 8; b_pos = 0; a_size = 0; a_pos = 0; break;
    case 32: r_size = 8; r_pos = 16; g_size = 8; g_pos = 8;
             b_size = 8; b_pos = 0; a_size = 8; a_pos = 24; break;
    default: r_size = 0; r_pos = 0; g_size = 0; g_pos = 0;
             b_size = 0; b_pos = 0; a_size = 0; a_pos = 0; break;
    }

    SET_FARVAR(seg, info->red_size, r_size);
    SET_FARVAR(seg, info->red_pos, r_pos);
    SET_FARVAR(seg, info->green_size, g_size);
    SET_FARVAR(seg, info->green_pos, g_pos);
    SET_FARVAR(seg, info->blue_size, b_size);
    SET_FARVAR(seg, info->blue_pos, b_pos);
    SET_FARVAR(seg, info->alpha_size, a_size);
    SET_FARVAR(seg, info->alpha_pos, a_pos);

    if (modeinfo.depth == 32)
        SET_FARVAR(seg, info->directcolor_info,
                   VBE_DIRECTCOLOR_RESERVED_BITS_AVAILABLE);
    else
        SET_FARVAR(seg, info->directcolor_info, 0);

    if (modeinfo.depth > 4)
        SET_FARVAR(seg, info->phys_base, modeinfo.phys_base);
    else
        SET_FARVAR(seg, info->phys_base, 0);

    SET_FARVAR(seg, info->reserved1, 0);
    SET_FARVAR(seg, info->reserved2, 0);
    SET_FARVAR(seg, info->linear_bytes_per_scanline, modeinfo.linesize);
    SET_FARVAR(seg, info->bank_pages, 0);
    SET_FARVAR(seg, info->linear_pages, 0);
    SET_FARVAR(seg, info->linear_red_size, r_size);
    SET_FARVAR(seg, info->linear_red_pos, r_pos);
    SET_FARVAR(seg, info->linear_green_size, g_size);
    SET_FARVAR(seg, info->linear_green_pos, g_pos);
    SET_FARVAR(seg, info->linear_blue_size, b_size);
    SET_FARVAR(seg, info->linear_blue_pos, b_pos);
    SET_FARVAR(seg, info->linear_alpha_size, a_size);
    SET_FARVAR(seg, info->linear_alpha_pos, a_pos);
    SET_FARVAR(seg, info->pixclock_max, 0);

    regs->al = regs->ah; /* 0x4F, Function supported */
    regs->ah = 0x0; /* 0x0, Function call successful */
}

static void
vbe_104f02(struct bregs *regs)
{
    dprintf(1, "VBE mode set: %x\n", regs->bx);

    int mode = regs->bx & 0x1ff;
    int flags = regs->bx & (MF_CUSTOMCRTC|MF_LINEARFB|MF_NOCLEARMEM);
    int ret = vgahw_set_mode(mode, flags);

    regs->ah = ret;
    regs->al = 0x4f;
}

static void
vbe_104f03(struct bregs *regs)
{
    if (!bochsvga_hires_enabled()) {
        regs->bx = GET_BDA(video_mode);
    } else {
        regs->bx = bochsvga_curr_mode();
    }

    dprintf(1, "VBE current mode=%x\n", regs->bx);

    regs->al = regs->ah; /* 0x4F, Function supported */
    regs->ah = 0x0; /* 0x0, Function call successful */
}

static void
vbe_104f04(struct bregs *regs)
{
    debug_stub(regs);
    regs->ax = 0x0100;
}

static void
vbe_104f05(struct bregs *regs)
{
    debug_stub(regs);
    regs->ax = 0x0100;
}

static void
vbe_104f06(struct bregs *regs)
{
    debug_stub(regs);
    regs->ax = 0x0100;
}

static void
vbe_104f07(struct bregs *regs)
{
    debug_stub(regs);
    regs->ax = 0x0100;
}

static void
vbe_104f08(struct bregs *regs)
{
    debug_stub(regs);
    regs->ax = 0x0100;
}

static void
vbe_104f0a(struct bregs *regs)
{
    debug_stub(regs);
    regs->ax = 0x0100;
}

static void
vbe_104fXX(struct bregs *regs)
{
    debug_stub(regs);
    regs->ax = 0x0100;
}

void
handle_104f(struct bregs *regs)
{
    if (!bochsvga_enabled()) {
        vbe_104fXX(regs);
        return;
    }

    switch (regs->al) {
    case 0x00: vbe_104f00(regs); break;
    case 0x01: vbe_104f01(regs); break;
    case 0x02: vbe_104f02(regs); break;
    case 0x03: vbe_104f03(regs); break;
    case 0x04: vbe_104f04(regs); break;
    case 0x05: vbe_104f05(regs); break;
    case 0x06: vbe_104f06(regs); break;
    case 0x07: vbe_104f07(regs); break;
    case 0x08: vbe_104f08(regs); break;
    case 0x0a: vbe_104f0a(regs); break;
    default:   vbe_104fXX(regs); break;
    }
}
