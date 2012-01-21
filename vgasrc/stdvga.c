// Standard VGA driver code
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2001-2008 the LGPL VGABios developers Team
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "stdvga.h" // stdvga_init
#include "ioport.h" // outb
#include "farptr.h" // SET_FARVAR
#include "biosvar.h" // GET_GLOBAL
#include "util.h" // memcpy_far


/****************************************************************
 * Attribute control
 ****************************************************************/

void
stdvga_set_border_color(u8 color)
{
    u8 v1 = color & 0x0f;
    if (v1 & 0x08)
        v1 += 0x08;
    stdvga_attr_write(0x00, v1);

    int i;
    for (i = 1; i < 4; i++)
        stdvga_attr_mask(i, 0x10, color & 0x10);
}

void
stdvga_set_overscan_border_color(u8 color)
{
    stdvga_attr_write(0x11, color);
}

u8
stdvga_get_overscan_border_color(void)
{
    return stdvga_attr_read(0x11);
}

void
stdvga_set_palette(u8 palid)
{
    int i;
    for (i = 1; i < 4; i++)
        stdvga_attr_mask(i, 0x01, palid & 0x01);
}

void
stdvga_set_all_palette_reg(u16 seg, u8 *data_far)
{
    int i;
    for (i = 0; i < 0x10; i++) {
        stdvga_attr_write(i, GET_FARVAR(seg, *data_far));
        data_far++;
    }
    stdvga_attr_write(0x11, GET_FARVAR(seg, *data_far));
}

void
stdvga_get_all_palette_reg(u16 seg, u8 *data_far)
{
    int i;
    for (i = 0; i < 0x10; i++) {
        SET_FARVAR(seg, *data_far, stdvga_attr_read(i));
        data_far++;
    }
    SET_FARVAR(seg, *data_far, stdvga_attr_read(0x11));
}

void
stdvga_toggle_intensity(u8 flag)
{
    stdvga_attr_mask(0x10, 0x08, (flag & 0x01) << 3);
}

void
stdvga_select_video_dac_color_page(u8 flag, u8 data)
{
    if (!(flag & 0x01)) {
        // select paging mode
        stdvga_attr_mask(0x10, 0x80, data << 7);
        return;
    }
    // select page
    u8 val = stdvga_attr_read(0x10);
    if (!(val & 0x80))
        data <<= 2;
    data &= 0x0f;
    stdvga_attr_write(0x14, data);
}

void
stdvga_read_video_dac_state(u8 *pmode, u8 *curpage)
{
    u8 val1 = stdvga_attr_read(0x10) >> 7;
    u8 val2 = stdvga_attr_read(0x14) & 0x0f;
    if (!(val1 & 0x01))
        val2 >>= 2;
    *pmode = val1;
    *curpage = val2;
}


/****************************************************************
 * DAC control
 ****************************************************************/

void
stdvga_save_dac_state(u16 seg, struct saveDACcolors *info)
{
    /* XXX: check this */
    SET_FARVAR(seg, info->rwmode, inb(VGAREG_DAC_STATE));
    SET_FARVAR(seg, info->peladdr, inb(VGAREG_DAC_WRITE_ADDRESS));
    SET_FARVAR(seg, info->pelmask, stdvga_pelmask_read());
    stdvga_dac_read(seg, info->dac, 0, 256);
    SET_FARVAR(seg, info->color_select, 0);
}

void
stdvga_restore_dac_state(u16 seg, struct saveDACcolors *info)
{
    stdvga_pelmask_write(GET_FARVAR(seg, info->pelmask));
    stdvga_dac_write(seg, info->dac, 0, 256);
    outb(GET_FARVAR(seg, info->peladdr), VGAREG_DAC_WRITE_ADDRESS);
}

void
stdvga_perform_gray_scale_summing(u16 start, u16 count)
{
    stdvga_attrindex_write(0x00);
    int i;
    for (i = start; i < start+count; i++) {
        u8 rgb[3];
        stdvga_dac_read(GET_SEG(SS), rgb, i, 1);

        // intensity = ( 0.3 * Red ) + ( 0.59 * Green ) + ( 0.11 * Blue )
        u16 intensity = ((77 * rgb[0] + 151 * rgb[1] + 28 * rgb[2]) + 0x80) >> 8;
        if (intensity > 0x3f)
            intensity = 0x3f;

        stdvga_dac_write(GET_SEG(SS), rgb, i, 1);
    }
    stdvga_attrindex_write(0x20);
}


/****************************************************************
 * Memory control
 ****************************************************************/

void
stdvga_set_text_block_specifier(u8 spec)
{
    stdvga_sequ_write(0x03, spec);
}

// Enable reads and writes to the given "plane" when in planar4 mode.
void
stdvga_planar4_plane(int plane)
{
    if (plane < 0) {
        // Return to default mode (read plane0, write all planes)
        stdvga_sequ_write(0x02, 0x0f);
        stdvga_grdc_write(0x04, 0);
    } else {
        stdvga_sequ_write(0x02, 1<<plane);
        stdvga_grdc_write(0x04, plane);
    }
}


/****************************************************************
 * Font loading
 ****************************************************************/

static void
get_font_access(void)
{
    stdvga_sequ_write(0x00, 0x01);
    stdvga_sequ_write(0x02, 0x04);
    stdvga_sequ_write(0x04, 0x07);
    stdvga_sequ_write(0x00, 0x03);
    stdvga_grdc_write(0x04, 0x02);
    stdvga_grdc_write(0x05, 0x00);
    stdvga_grdc_write(0x06, 0x04);
}

static void
release_font_access(void)
{
    stdvga_sequ_write(0x00, 0x01);
    stdvga_sequ_write(0x02, 0x03);
    stdvga_sequ_write(0x04, 0x03);
    stdvga_sequ_write(0x00, 0x03);
    u16 v = (stdvga_misc_read() & 0x01) ? 0x0e : 0x0a;
    stdvga_grdc_write(0x06, v);
    stdvga_grdc_write(0x04, 0x00);
    stdvga_grdc_write(0x05, 0x10);
}

void
stdvga_load_font(u16 seg, void *src_far, u16 count
                 , u16 start, u8 destflags, u8 fontsize)
{
    get_font_access();
    u16 blockaddr = ((destflags & 0x03) << 14) + ((destflags & 0x04) << 11);
    void *dest_far = (void*)(blockaddr + start*32);
    u16 i;
    for (i = 0; i < count; i++)
        memcpy_far(SEG_GRAPH, dest_far + i*32
                   , seg, src_far + i*fontsize, fontsize);
    release_font_access();
}


/****************************************************************
 * CRTC registers
 ****************************************************************/

u16
stdvga_get_crtc(void)
{
    if (stdvga_misc_read() & 1)
        return VGAREG_VGA_CRTC_ADDRESS;
    return VGAREG_MDA_CRTC_ADDRESS;
}

void
stdvga_set_cursor_shape(u8 start, u8 end)
{
    u16 crtc_addr = stdvga_get_crtc();
    stdvga_crtc_write(crtc_addr, 0x0a, start);
    stdvga_crtc_write(crtc_addr, 0x0b, end);
}

void
stdvga_set_active_page(u16 address)
{
    u16 crtc_addr = stdvga_get_crtc();
    stdvga_crtc_write(crtc_addr, 0x0c, address >> 8);
    stdvga_crtc_write(crtc_addr, 0x0d, address);
}

void
stdvga_set_cursor_pos(u16 address)
{
    u16 crtc_addr = stdvga_get_crtc();
    stdvga_crtc_write(crtc_addr, 0x0e, address >> 8);
    stdvga_crtc_write(crtc_addr, 0x0f, address);
}

void
stdvga_set_scan_lines(u8 lines)
{
    stdvga_crtc_mask(stdvga_get_crtc(), 0x09, 0x1f, lines - 1);
}

// Get vertical display end
u16
stdvga_get_vde(void)
{
    u16 crtc_addr = stdvga_get_crtc();
    u16 vde = stdvga_crtc_read(crtc_addr, 0x12);
    u8 ovl = stdvga_crtc_read(crtc_addr, 0x07);
    vde += (((ovl & 0x02) << 7) + ((ovl & 0x40) << 3) + 1);
    return vde;
}


/****************************************************************
 * Save/Restore/Set state
 ****************************************************************/

void
stdvga_save_state(u16 seg, struct saveVideoHardware *info)
{
    u16 crtc_addr = stdvga_get_crtc();
    SET_FARVAR(seg, info->sequ_index, inb(VGAREG_SEQU_ADDRESS));
    SET_FARVAR(seg, info->crtc_index, inb(crtc_addr));
    SET_FARVAR(seg, info->grdc_index, inb(VGAREG_GRDC_ADDRESS));
    SET_FARVAR(seg, info->actl_index, stdvga_attrindex_read());
    SET_FARVAR(seg, info->feature, inb(VGAREG_READ_FEATURE_CTL));

    int i;
    for (i=0; i<4; i++)
        SET_FARVAR(seg, info->sequ_regs[i], stdvga_sequ_read(i+1));
    SET_FARVAR(seg, info->sequ0, stdvga_sequ_read(0));

    for (i=0; i<25; i++)
        SET_FARVAR(seg, info->crtc_regs[i], stdvga_crtc_read(crtc_addr, i));

    for (i=0; i<20; i++)
        SET_FARVAR(seg, info->actl_regs[i], stdvga_attr_read(i));

    for (i=0; i<9; i++)
        SET_FARVAR(seg, info->grdc_regs[i], stdvga_grdc_read(i));

    SET_FARVAR(seg, info->crtc_addr, crtc_addr);

    /* XXX: read plane latches */
    for (i=0; i<4; i++)
        SET_FARVAR(seg, info->plane_latch[i], 0);
}

void
stdvga_restore_state(u16 seg, struct saveVideoHardware *info)
{
    int i;
    for (i=0; i<4; i++)
        stdvga_sequ_write(i+1, GET_FARVAR(seg, info->sequ_regs[i]));
    stdvga_sequ_write(0x00, GET_FARVAR(seg, info->sequ0));

    // Disable CRTC write protection
    u16 crtc_addr = GET_FARVAR(seg, info->crtc_addr);
    stdvga_crtc_write(crtc_addr, 0x11, 0x00);
    // Set CRTC regs
    for (i=0; i<25; i++)
        if (i != 0x11)
            stdvga_crtc_write(crtc_addr, i, GET_FARVAR(seg, info->crtc_regs[i]));
    // select crtc base address
    stdvga_misc_mask(0x01, crtc_addr == VGAREG_VGA_CRTC_ADDRESS ? 0x01 : 0x00);

    // enable write protection if needed
    stdvga_crtc_write(crtc_addr, 0x11, GET_FARVAR(seg, info->crtc_regs[0x11]));

    // Set Attribute Ctl
    for (i=0; i<20; i++)
        stdvga_attr_write(i, GET_FARVAR(seg, info->actl_regs[i]));
    stdvga_attrindex_write(GET_FARVAR(seg, info->actl_index));

    for (i=0; i<9; i++)
        stdvga_grdc_write(i, GET_FARVAR(seg, info->grdc_regs[i]));

    outb(GET_FARVAR(seg, info->sequ_index), VGAREG_SEQU_ADDRESS);
    outb(GET_FARVAR(seg, info->crtc_index), crtc_addr);
    outb(GET_FARVAR(seg, info->grdc_index), VGAREG_GRDC_ADDRESS);
    outb(GET_FARVAR(seg, info->feature), crtc_addr - 0x4 + 0xa);
}

static void
clear_screen(struct vgamode_s *vmode_g)
{
    switch (GET_GLOBAL(vmode_g->memmodel)) {
    case MM_TEXT:
        memset16_far(GET_GLOBAL(vmode_g->sstart), 0, 0x0720, 32*1024);
        break;
    case MM_CGA:
        memset16_far(GET_GLOBAL(vmode_g->sstart), 0, 0x0000, 32*1024);
        break;
    default:
        // XXX - old code gets/sets/restores sequ register 2 to 0xf -
        // but it should always be 0xf anyway.
        memset16_far(GET_GLOBAL(vmode_g->sstart), 0, 0x0000, 64*1024);
    }
}

int
stdvga_set_mode(struct vgamode_s *vmode_g, int flags)
{
    if (! stdvga_is_mode(vmode_g)) {
        warn_internalerror();
        return -1;
    }
    struct stdvga_mode_s *stdmode_g = container_of(
        vmode_g, struct stdvga_mode_s, info);

    // if palette loading (bit 3 of modeset ctl = 0)
    if (!(flags & MF_NOPALETTE)) {    // Set the PEL mask
        stdvga_pelmask_write(GET_GLOBAL(stdmode_g->pelmask));

        // From which palette
        u8 *palette_g = GET_GLOBAL(stdmode_g->dac);
        u16 palsize = GET_GLOBAL(stdmode_g->dacsize) / 3;

        // Always 256*3 values
        stdvga_dac_write(get_global_seg(), palette_g, 0, palsize);
        int i;
        for (i = palsize; i < 0x0100; i++) {
            static u8 rgb[3] VAR16;
            stdvga_dac_write(get_global_seg(), rgb, i, 1);
        }

        if (flags & MF_GRAYSUM)
            stdvga_perform_gray_scale_summing(0x00, 0x100);
    }

    // Set Attribute Ctl
    u8 *regs = GET_GLOBAL(stdmode_g->actl_regs);
    int i;
    for (i = 0; i <= 0x13; i++)
        stdvga_attr_write(i, GET_GLOBAL(regs[i]));
    stdvga_attr_write(0x14, 0x00);

    // Set Sequencer Ctl
    stdvga_sequ_write(0x00, 0x03);
    regs = GET_GLOBAL(stdmode_g->sequ_regs);
    for (i = 1; i <= 4; i++)
        stdvga_sequ_write(i, GET_GLOBAL(regs[i - 1]));

    // Set Grafx Ctl
    regs = GET_GLOBAL(stdmode_g->grdc_regs);
    for (i = 0; i <= 8; i++)
        stdvga_grdc_write(i, GET_GLOBAL(regs[i]));

    // Set CRTC address VGA or MDA
    u8 miscreg = GET_GLOBAL(stdmode_g->miscreg);
    u16 crtc_addr = VGAREG_VGA_CRTC_ADDRESS;
    if (!(miscreg & 1))
        crtc_addr = VGAREG_MDA_CRTC_ADDRESS;

    // Disable CRTC write protection
    stdvga_crtc_write(crtc_addr, 0x11, 0x00);
    // Set CRTC regs
    regs = GET_GLOBAL(stdmode_g->crtc_regs);
    for (i = 0; i <= 0x18; i++)
        stdvga_crtc_write(crtc_addr, i, GET_GLOBAL(regs[i]));

    // Set the misc register
    stdvga_misc_write(miscreg);

    // Enable video
    stdvga_attrindex_write(0x20);

    // Clear screen
    if (!(flags & MF_NOCLEARMEM))
        clear_screen(vmode_g);

    // Write the fonts in memory
    u8 memmodel = GET_GLOBAL(vmode_g->memmodel);
    if (memmodel == MM_TEXT)
        stdvga_load_font(get_global_seg(), vgafont16, 0x100, 0, 0, 16);

    return 0;
}


/****************************************************************
 * Misc
 ****************************************************************/

void
stdvga_list_modes(u16 seg, u16 *dest, u16 *last)
{
    SET_FARVAR(seg, *dest, 0xffff);
}

void
stdvga_enable_video_addressing(u8 disable)
{
    u8 v = (disable & 1) ? 0x00 : 0x02;
    stdvga_misc_mask(0x02, v);
}

int
stdvga_init(void)
{
    // switch to color mode and enable CPU access 480 lines
    stdvga_misc_write(0xc3);
    // more than 64k 3C4/04
    stdvga_sequ_write(0x04, 0x02);

    return 0;
}
