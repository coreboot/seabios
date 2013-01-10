// VGA bios implementation
//
// Copyright (C) 2009-2012  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2001-2008 the LGPL VGABios developers Team
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "bregs.h" // struct bregs
#include "biosvar.h" // GET_BDA
#include "util.h" // memset
#include "vgabios.h" // calc_page_size
#include "optionroms.h" // struct pci_data
#include "config.h" // CONFIG_*
#include "stdvga.h" // stdvga_set_cursor_shape
#include "clext.h" // clext_1012
#include "vgahw.h" // vgahw_set_mode
#include "vbe.h" // VBE_RETURN_STATUS_FAILED
#include "pci.h" // pci_config_readw
#include "pci_regs.h" // PCI_VENDOR_ID

// Standard Video Save Pointer Table
struct VideoSavePointer_s {
    struct segoff_s videoparam;
    struct segoff_s paramdynamicsave;
    struct segoff_s textcharset;
    struct segoff_s graphcharset;
    struct segoff_s secsavepointer;
    u8 reserved[8];
} PACKED;

static struct VideoSavePointer_s video_save_pointer_table VAR16;

struct VideoParam_s video_param_table[29] VAR16;


/****************************************************************
 * PCI Data
 ****************************************************************/

struct pci_data rom_pci_data VAR16VISIBLE = {
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
 * Helper functions
 ****************************************************************/

// Return the bits per pixel in system memory for a given mode.
int
vga_bpp(struct vgamode_s *vmode_g)
{
    switch (GET_GLOBAL(vmode_g->memmodel)) {
    case MM_TEXT:
        return 16;
    case MM_PLANAR:
        return 1;
    }
    u8 depth = GET_GLOBAL(vmode_g->depth);
    if (depth > 8)
        return ALIGN(depth, 8);
    return depth;
}

u16
calc_page_size(u8 memmodel, u16 width, u16 height)
{
    switch (memmodel) {
    case MM_TEXT:
        return ALIGN(width * height * 2, 2*1024);
    case MM_CGA:
        return 16*1024;
    default:
        return ALIGN(width * height / 8, 8*1024);
    }
}

static void
set_cursor_shape(u8 start, u8 end)
{
    start &= 0x3f;
    end &= 0x1f;

    u16 curs = (start << 8) + end;
    SET_BDA(cursor_type, curs);

    u8 modeset_ctl = GET_BDA(modeset_ctl);
    u16 cheight = GET_BDA(char_height);
    if ((modeset_ctl & 0x01) && (cheight > 8) && (end < 8) && (start < 0x20)) {
        if (end != (start + 1))
            start = ((start + 1) * cheight / 8) - 1;
        else
            start = ((end + 1) * cheight / 8) - 2;
        end = ((end + 1) * cheight / 8) - 1;
    }
    stdvga_set_cursor_shape(start, end);
}

static u16
get_cursor_shape(u8 page)
{
    if (page > 7)
        return 0;
    // FIXME should handle VGA 14/16 lines
    return GET_BDA(cursor_type);
}

static void
set_cursor_pos(struct cursorpos cp)
{
    u8 page = cp.page, x = cp.x, y = cp.y;

    // Should not happen...
    if (page > 7)
        return;

    // Bios cursor pos
    SET_BDA(cursor_pos[page], (y << 8) | x);

    // Set the hardware cursor
    u8 current = GET_BDA(video_page);
    if (cp.page != current)
        return;

    // Calculate the memory address
    int address = (GET_BDA(video_pagesize) * page
                   + (x + y * GET_BDA(video_cols)) * 2);
    stdvga_set_cursor_pos(address);
}

static struct cursorpos
get_cursor_pos(u8 page)
{
    if (page == 0xff)
        // special case - use current page
        page = GET_BDA(video_page);
    if (page > 7) {
        struct cursorpos cp = { 0, 0, 0xfe };
        return cp;
    }
    // FIXME should handle VGA 14/16 lines
    u16 xy = GET_BDA(cursor_pos[page]);
    struct cursorpos cp = {xy, xy>>8, page};
    return cp;
}

static void
set_active_page(u8 page)
{
    if (page > 7)
        return;

    // Get the mode
    struct vgamode_s *vmode_g = get_current_mode();
    if (!vmode_g)
        return;

    // Get cursor pos for the given page
    struct cursorpos cp = get_cursor_pos(page);

    // Calculate memory address of start of page
    int address = GET_BDA(video_pagesize) * page;
    vgahw_set_displaystart(vmode_g, address);

    // And change the BIOS page
    SET_BDA(video_pagestart, address);
    SET_BDA(video_page, page);

    dprintf(1, "Set active page %02x address %04x\n", page, address);

    // Display the cursor, now the page is active
    set_cursor_pos(cp);
}

static void
set_scan_lines(u8 lines)
{
    stdvga_set_scan_lines(lines);
    if (lines == 8)
        set_cursor_shape(0x06, 0x07);
    else
        set_cursor_shape(lines - 4, lines - 3);
    SET_BDA(char_height, lines);
    u16 vde = stdvga_get_vde();
    u8 rows = vde / lines;
    SET_BDA(video_rows, rows - 1);
    u16 cols = GET_BDA(video_cols);
    SET_BDA(video_pagesize, calc_page_size(MM_TEXT, cols, rows));
}


/****************************************************************
 * Character writing
 ****************************************************************/

// Scroll the screen one line.  This function is designed to be called
// tail-recursive to reduce stack usage.
static void noinline
scroll_one(u16 nbrows, u16 nbcols, u8 page)
{
    struct cursorpos ul = {0, 0, page};
    struct cursorpos lr = {nbcols-1, nbrows-1, page};
    vgafb_scroll(1, -1, ul, lr);
}

// Write a character to the screen at a given position.  Implement
// special characters and scroll the screen if necessary.
static void
write_teletype(struct cursorpos *pcp, struct carattr ca)
{
    struct cursorpos cp = *pcp;

    // Get the dimensions
    u16 nbrows = GET_BDA(video_rows) + 1;
    u16 nbcols = GET_BDA(video_cols);

    switch (ca.car) {
    case 7:
        //FIXME should beep
        break;
    case 8:
        if (cp.x > 0)
            cp.x--;
        break;
    case '\r':
        cp.x = 0;
        break;
    case '\n':
        cp.y++;
        break;
    case '\t':
        ca.car = ' ';
        do {
            vgafb_write_char(cp, ca);
            cp.x++;
        } while (cp.x < nbcols && cp.x % 8);
        break;
    default:
        vgafb_write_char(cp, ca);
        cp.x++;
    }

    // Do we need to wrap ?
    if (cp.x == nbcols) {
        cp.x = 0;
        cp.y++;
    }
    // Do we need to scroll ?
    if (cp.y < nbrows) {
        *pcp = cp;
        return;
    }
    // Scroll screen
    cp.y--;
    *pcp = cp;
    scroll_one(nbrows, nbcols, cp.page);
}


/****************************************************************
 * Save and restore bda state
 ****************************************************************/

void
save_bda_state(u16 seg, struct saveBDAstate *info)
{
    SET_FARVAR(seg, info->video_mode, GET_BDA(vbe_mode));
    SET_FARVAR(seg, info->video_cols, GET_BDA(video_cols));
    SET_FARVAR(seg, info->video_pagesize, GET_BDA(video_pagesize));
    SET_FARVAR(seg, info->crtc_address, GET_BDA(crtc_address));
    SET_FARVAR(seg, info->video_rows, GET_BDA(video_rows));
    SET_FARVAR(seg, info->char_height, GET_BDA(char_height));
    SET_FARVAR(seg, info->video_ctl, GET_BDA(video_ctl));
    SET_FARVAR(seg, info->video_switches, GET_BDA(video_switches));
    SET_FARVAR(seg, info->modeset_ctl, GET_BDA(modeset_ctl));
    SET_FARVAR(seg, info->cursor_type, GET_BDA(cursor_type));
    int i;
    for (i=0; i<8; i++)
        SET_FARVAR(seg, info->cursor_pos[i], GET_BDA(cursor_pos[i]));
    SET_FARVAR(seg, info->video_pagestart, GET_BDA(video_pagestart));
    SET_FARVAR(seg, info->video_page, GET_BDA(video_page));
    /* current font */
    SET_FARVAR(seg, info->font0, GET_IVT(0x1f));
    SET_FARVAR(seg, info->font1, GET_IVT(0x43));
}

void
restore_bda_state(u16 seg, struct saveBDAstate *info)
{
    u16 mode = GET_FARVAR(seg, info->video_mode);
    SET_BDA(vbe_mode, mode);
    if (mode < 0x100)
        SET_BDA(video_mode, mode);
    else
        SET_BDA(video_mode, 0xff);
    SET_BDA(video_cols, GET_FARVAR(seg, info->video_cols));
    SET_BDA(video_pagesize, GET_FARVAR(seg, info->video_pagesize));
    SET_BDA(crtc_address, GET_FARVAR(seg, info->crtc_address));
    SET_BDA(video_rows, GET_FARVAR(seg, info->video_rows));
    SET_BDA(char_height, GET_FARVAR(seg, info->char_height));
    SET_BDA(video_ctl, GET_FARVAR(seg, info->video_ctl));
    SET_BDA(video_switches, GET_FARVAR(seg, info->video_switches));
    SET_BDA(modeset_ctl, GET_FARVAR(seg, info->modeset_ctl));
    SET_BDA(cursor_type, GET_FARVAR(seg, info->cursor_type));
    int i;
    for (i = 0; i < 8; i++)
        SET_BDA(cursor_pos[i], GET_FARVAR(seg, info->cursor_pos[i]));
    SET_BDA(video_pagestart, GET_FARVAR(seg, info->video_pagestart));
    SET_BDA(video_page, GET_FARVAR(seg, info->video_page));
    /* current font */
    SET_IVT(0x1f, GET_FARVAR(seg, info->font0));
    SET_IVT(0x43, GET_FARVAR(seg, info->font1));
}


/****************************************************************
 * Mode setting
 ****************************************************************/

struct vgamode_s *
get_current_mode(void)
{
    return vgahw_find_mode(GET_BDA(vbe_mode) & ~MF_VBEFLAGS);
}

// Setup BDA after a mode switch.
int
vga_set_mode(int mode, int flags)
{
    dprintf(1, "set VGA mode %x\n", mode);
    struct vgamode_s *vmode_g = vgahw_find_mode(mode);
    if (!vmode_g)
        return VBE_RETURN_STATUS_FAILED;

    int ret = vgahw_set_mode(vmode_g, flags);
    if (ret)
        return ret;

    // Set the BIOS mem
    int width = GET_GLOBAL(vmode_g->width);
    int height = GET_GLOBAL(vmode_g->height);
    u8 memmodel = GET_GLOBAL(vmode_g->memmodel);
    int cheight = GET_GLOBAL(vmode_g->cheight);
    if (mode < 0x100)
        SET_BDA(video_mode, mode);
    else
        SET_BDA(video_mode, 0xff);
    SET_BDA(vbe_mode, mode | (flags & MF_VBEFLAGS));
    if (memmodel == MM_TEXT) {
        SET_BDA(video_cols, width);
        SET_BDA(video_rows, height-1);
        SET_BDA(cursor_type, 0x0607);
    } else {
        int cwidth = GET_GLOBAL(vmode_g->cwidth);
        SET_BDA(video_cols, width / cwidth);
        SET_BDA(video_rows, (height / cheight) - 1);
        SET_BDA(cursor_type, 0x0000);
    }
    SET_BDA(video_pagesize, calc_page_size(memmodel, width, height));
    SET_BDA(crtc_address, stdvga_get_crtc());
    SET_BDA(char_height, cheight);
    SET_BDA(video_ctl, 0x60 | (flags & MF_NOCLEARMEM ? 0x80 : 0x00));
    SET_BDA(video_switches, 0xF9);
    SET_BDA(modeset_ctl, GET_BDA(modeset_ctl) & 0x7f);
    int i;
    for (i=0; i<8; i++)
        SET_BDA(cursor_pos[i], 0x0000);
    SET_BDA(video_pagestart, 0x0000);
    SET_BDA(video_page, 0x00);

    // FIXME We nearly have the good tables. to be reworked
    SET_BDA(dcc_index, 0x08);   // 8 is VGA should be ok for now
    SET_BDA(video_savetable
            , SEGOFF(get_global_seg(), (u32)&video_save_pointer_table));

    // FIXME
    SET_BDA(video_msr, 0x00); // Unavailable on vanilla vga, but...
    SET_BDA(video_pal, 0x00); // Unavailable on vanilla vga, but...

    // Set the ints 0x1F and 0x43
    SET_IVT(0x1f, SEGOFF(get_global_seg(), (u32)&vgafont8[128 * 8]));

    switch (cheight) {
    case 8:
        SET_IVT(0x43, SEGOFF(get_global_seg(), (u32)vgafont8));
        break;
    case 14:
        SET_IVT(0x43, SEGOFF(get_global_seg(), (u32)vgafont14));
        break;
    case 16:
        SET_IVT(0x43, SEGOFF(get_global_seg(), (u32)vgafont16));
        break;
    }

    return 0;
}


/****************************************************************
 * VGA int 10 handler
 ****************************************************************/

static void
handle_1000(struct bregs *regs)
{
    int mode = regs->al & 0x7f;

    // Set regs->al
    if (mode > 7)
        regs->al = 0x20;
    else if (mode == 6)
        regs->al = 0x3f;
    else
        regs->al = 0x30;

    int flags = GET_BDA(modeset_ctl) & (MF_NOPALETTE|MF_GRAYSUM);
    if (regs->al & 0x80)
        flags |= MF_NOCLEARMEM;

    vga_set_mode(mode, flags);
}

static void
handle_1001(struct bregs *regs)
{
    set_cursor_shape(regs->ch, regs->cl);
}

static void
handle_1002(struct bregs *regs)
{
    struct cursorpos cp = {regs->dl, regs->dh, regs->bh};
    set_cursor_pos(cp);
}

static void
handle_1003(struct bregs *regs)
{
    regs->cx = get_cursor_shape(regs->bh);
    struct cursorpos cp = get_cursor_pos(regs->bh);
    regs->dl = cp.x;
    regs->dh = cp.y;
}

// Read light pen pos (unimplemented)
static void
handle_1004(struct bregs *regs)
{
    debug_stub(regs);
    regs->ax = regs->bx = regs->cx = regs->dx = 0;
}

static void
handle_1005(struct bregs *regs)
{
    set_active_page(regs->al);
}

static void
verify_scroll(struct bregs *regs, int dir)
{
    u8 ulx = regs->cl, uly = regs->ch, lrx = regs->dl, lry = regs->dh;
    u16 nbrows = GET_BDA(video_rows) + 1;
    if (lry >= nbrows)
        lry = nbrows - 1;
    u16 nbcols = GET_BDA(video_cols);
    if (lrx >= nbcols)
        lrx = nbcols - 1;

    if (ulx > lrx || uly > lry)
        return;

    int nblines = regs->al;
    if (!nblines || nblines > lry - uly + 1)
        nblines = lry - uly + 1;

    u8 page = GET_BDA(video_page);
    struct cursorpos ul = {ulx, uly, page};
    struct cursorpos lr = {lrx, lry, page};
    vgafb_scroll(dir * nblines, regs->bh, ul, lr);
}

static void
handle_1006(struct bregs *regs)
{
    verify_scroll(regs, 1);
}

static void
handle_1007(struct bregs *regs)
{
    verify_scroll(regs, -1);
}

static void
handle_1008(struct bregs *regs)
{
    struct carattr ca = vgafb_read_char(get_cursor_pos(regs->bh));
    regs->al = ca.car;
    regs->ah = ca.attr;
}

static void noinline
write_chars(u8 page, struct carattr ca, u16 count)
{
    u16 nbcols = GET_BDA(video_cols);
    struct cursorpos cp = get_cursor_pos(page);
    while (count--) {
        vgafb_write_char(cp, ca);
        cp.x++;
        if (cp.x >= nbcols) {
            cp.x -= nbcols;
            cp.y++;
        }
    }
}

static void
handle_1009(struct bregs *regs)
{
    struct carattr ca = {regs->al, regs->bl, 1};
    write_chars(regs->bh, ca, regs->cx);
}

static void
handle_100a(struct bregs *regs)
{
    struct carattr ca = {regs->al, regs->bl, 0};
    write_chars(regs->bh, ca, regs->cx);
}


static void
handle_100b00(struct bregs *regs)
{
    stdvga_set_border_color(regs->bl);
}

static void
handle_100b01(struct bregs *regs)
{
    stdvga_set_palette(regs->bl);
}

static void
handle_100bXX(struct bregs *regs)
{
    debug_stub(regs);
}

static void
handle_100b(struct bregs *regs)
{
    switch (regs->bh) {
    case 0x00: handle_100b00(regs); break;
    case 0x01: handle_100b01(regs); break;
    default:   handle_100bXX(regs); break;
    }
}


static void
handle_100c(struct bregs *regs)
{
    // XXX - page (regs->bh) is unused
    vgafb_write_pixel(regs->al, regs->cx, regs->dx);
}

static void
handle_100d(struct bregs *regs)
{
    // XXX - page (regs->bh) is unused
    regs->al = vgafb_read_pixel(regs->cx, regs->dx);
}

static void noinline
handle_100e(struct bregs *regs)
{
    // Ralf Brown Interrupt list is WRONG on bh(page)
    // We do output only on the current page !
    struct carattr ca = {regs->al, regs->bl, 0};
    struct cursorpos cp = get_cursor_pos(0xff);
    write_teletype(&cp, ca);
    set_cursor_pos(cp);
}

static void
handle_100f(struct bregs *regs)
{
    regs->bh = GET_BDA(video_page);
    regs->al = GET_BDA(video_mode) | (GET_BDA(video_ctl) & 0x80);
    regs->ah = GET_BDA(video_cols);
}


static void
handle_101000(struct bregs *regs)
{
    if (regs->bl > 0x14)
        return;
    stdvga_attr_write(regs->bl, regs->bh);
}

static void
handle_101001(struct bregs *regs)
{
    stdvga_set_overscan_border_color(regs->bh);
}

static void
handle_101002(struct bregs *regs)
{
    stdvga_set_all_palette_reg(regs->es, (u8*)(regs->dx + 0));
}

static void
handle_101003(struct bregs *regs)
{
    stdvga_toggle_intensity(regs->bl);
}

static void
handle_101007(struct bregs *regs)
{
    if (regs->bl > 0x14)
        return;
    regs->bh = stdvga_attr_read(regs->bl);
}

static void
handle_101008(struct bregs *regs)
{
    regs->bh = stdvga_get_overscan_border_color();
}

static void
handle_101009(struct bregs *regs)
{
    stdvga_get_all_palette_reg(regs->es, (u8*)(regs->dx + 0));
}

static void noinline
handle_101010(struct bregs *regs)
{
    u8 rgb[3] = {regs->dh, regs->ch, regs->cl};
    stdvga_dac_write(GET_SEG(SS), rgb, regs->bx, 1);
}

static void
handle_101012(struct bregs *regs)
{
    stdvga_dac_write(regs->es, (u8*)(regs->dx + 0), regs->bx, regs->cx);
}

static void
handle_101013(struct bregs *regs)
{
    stdvga_select_video_dac_color_page(regs->bl, regs->bh);
}

static void noinline
handle_101015(struct bregs *regs)
{
    u8 rgb[3];
    stdvga_dac_read(GET_SEG(SS), rgb, regs->bx, 1);
    regs->dh = rgb[0];
    regs->ch = rgb[1];
    regs->cl = rgb[2];
}

static void
handle_101017(struct bregs *regs)
{
    stdvga_dac_read(regs->es, (u8*)(regs->dx + 0), regs->bx, regs->cx);
}

static void
handle_101018(struct bregs *regs)
{
    stdvga_pelmask_write(regs->bl);
}

static void
handle_101019(struct bregs *regs)
{
    regs->bl = stdvga_pelmask_read();
}

static void
handle_10101a(struct bregs *regs)
{
    stdvga_read_video_dac_state(&regs->bl, &regs->bh);
}

static void
handle_10101b(struct bregs *regs)
{
    stdvga_perform_gray_scale_summing(regs->bx, regs->cx);
}

static void
handle_1010XX(struct bregs *regs)
{
    debug_stub(regs);
}

static void
handle_1010(struct bregs *regs)
{
    switch (regs->al) {
    case 0x00: handle_101000(regs); break;
    case 0x01: handle_101001(regs); break;
    case 0x02: handle_101002(regs); break;
    case 0x03: handle_101003(regs); break;
    case 0x07: handle_101007(regs); break;
    case 0x08: handle_101008(regs); break;
    case 0x09: handle_101009(regs); break;
    case 0x10: handle_101010(regs); break;
    case 0x12: handle_101012(regs); break;
    case 0x13: handle_101013(regs); break;
    case 0x15: handle_101015(regs); break;
    case 0x17: handle_101017(regs); break;
    case 0x18: handle_101018(regs); break;
    case 0x19: handle_101019(regs); break;
    case 0x1a: handle_10101a(regs); break;
    case 0x1b: handle_10101b(regs); break;
    default:   handle_1010XX(regs); break;
    }
}


static void
handle_101100(struct bregs *regs)
{
    stdvga_load_font(regs->es, (void*)(regs->bp+0), regs->cx
                     , regs->dx, regs->bl, regs->bh);
}

static void
handle_101101(struct bregs *regs)
{
    stdvga_load_font(get_global_seg(), vgafont14, 0x100, 0, regs->bl, 14);
}

static void
handle_101102(struct bregs *regs)
{
    stdvga_load_font(get_global_seg(), vgafont8, 0x100, 0, regs->bl, 8);
}

static void
handle_101103(struct bregs *regs)
{
    stdvga_set_text_block_specifier(regs->bl);
}

static void
handle_101104(struct bregs *regs)
{
    stdvga_load_font(get_global_seg(), vgafont16, 0x100, 0, regs->bl, 16);
}

static void
handle_101110(struct bregs *regs)
{
    stdvga_load_font(regs->es, (void*)(regs->bp+0), regs->cx
                     , regs->dx, regs->bl, regs->bh);
    set_scan_lines(regs->bh);
}

static void
handle_101111(struct bregs *regs)
{
    stdvga_load_font(get_global_seg(), vgafont14, 0x100, 0, regs->bl, 14);
    set_scan_lines(14);
}

static void
handle_101112(struct bregs *regs)
{
    stdvga_load_font(get_global_seg(), vgafont8, 0x100, 0, regs->bl, 8);
    set_scan_lines(8);
}

static void
handle_101114(struct bregs *regs)
{
    stdvga_load_font(get_global_seg(), vgafont16, 0x100, 0, regs->bl, 16);
    set_scan_lines(16);
}

static void
handle_101120(struct bregs *regs)
{
    SET_IVT(0x1f, SEGOFF(regs->es, regs->bp));
}

void
load_gfx_font(u16 seg, u16 off, u8 height, u8 bl, u8 dl)
{
    u8 rows;

    SET_IVT(0x43, SEGOFF(seg, off));
    switch(bl) {
    case 0:
        rows = dl;
        break;
    case 1:
        rows = 14;
        break;
    case 3:
        rows = 43;
        break;
    case 2:
    default:
        rows = 25;
        break;
    }
    SET_BDA(video_rows, rows - 1);
    SET_BDA(char_height, height);
}

static void
handle_101121(struct bregs *regs)
{
    load_gfx_font(regs->es, regs->bp, regs->cx, regs->bl, regs->dl);
}

static void
handle_101122(struct bregs *regs)
{
    load_gfx_font(get_global_seg(), (u32)vgafont14, 14, regs->bl, regs->dl);
}

static void
handle_101123(struct bregs *regs)
{
    load_gfx_font(get_global_seg(), (u32)vgafont8, 8, regs->bl, regs->dl);
}

static void
handle_101124(struct bregs *regs)
{
    load_gfx_font(get_global_seg(), (u32)vgafont16, 16, regs->bl, regs->dl);
}

static void
handle_101130(struct bregs *regs)
{
    switch (regs->bh) {
    case 0x00: {
        struct segoff_s so = GET_IVT(0x1f);
        regs->es = so.seg;
        regs->bp = so.offset;
        break;
    }
    case 0x01: {
        struct segoff_s so = GET_IVT(0x43);
        regs->es = so.seg;
        regs->bp = so.offset;
        break;
    }
    case 0x02:
        regs->es = get_global_seg();
        regs->bp = (u32)vgafont14;
        break;
    case 0x03:
        regs->es = get_global_seg();
        regs->bp = (u32)vgafont8;
        break;
    case 0x04:
        regs->es = get_global_seg();
        regs->bp = (u32)vgafont8 + 128 * 8;
        break;
    case 0x05:
        regs->es = get_global_seg();
        regs->bp = (u32)vgafont14alt;
        break;
    case 0x06:
        regs->es = get_global_seg();
        regs->bp = (u32)vgafont16;
        break;
    case 0x07:
        regs->es = get_global_seg();
        regs->bp = (u32)vgafont16alt;
        break;
    default:
        dprintf(1, "Get font info BH(%02x) was discarded\n", regs->bh);
        return;
    }
    // Set byte/char of on screen font
    regs->cx = GET_BDA(char_height) & 0xff;

    // Set Highest char row
    regs->dl = GET_BDA(video_rows);
}

static void
handle_1011XX(struct bregs *regs)
{
    debug_stub(regs);
}

static void
handle_1011(struct bregs *regs)
{
    switch (regs->al) {
    case 0x00: handle_101100(regs); break;
    case 0x01: handle_101101(regs); break;
    case 0x02: handle_101102(regs); break;
    case 0x03: handle_101103(regs); break;
    case 0x04: handle_101104(regs); break;
    case 0x10: handle_101110(regs); break;
    case 0x11: handle_101111(regs); break;
    case 0x12: handle_101112(regs); break;
    case 0x14: handle_101114(regs); break;
    case 0x30: handle_101130(regs); break;
    case 0x20: handle_101120(regs); break;
    case 0x21: handle_101121(regs); break;
    case 0x22: handle_101122(regs); break;
    case 0x23: handle_101123(regs); break;
    case 0x24: handle_101124(regs); break;
    default:   handle_1011XX(regs); break;
    }
}


static void
handle_101210(struct bregs *regs)
{
    u16 crtc_addr = GET_BDA(crtc_address);
    if (crtc_addr == VGAREG_MDA_CRTC_ADDRESS)
        regs->bx = 0x0103;
    else
        regs->bx = 0x0003;
    regs->cx = GET_BDA(video_switches) & 0x0f;
}

static void
handle_101230(struct bregs *regs)
{
    u8 mctl = GET_BDA(modeset_ctl);
    u8 vswt = GET_BDA(video_switches);
    switch (regs->al) {
    case 0x00:
        // 200 lines
        mctl = (mctl & ~0x10) | 0x80;
        vswt = (vswt & ~0x0f) | 0x08;
        break;
    case 0x01:
        // 350 lines
        mctl &= ~0x90;
        vswt = (vswt & ~0x0f) | 0x09;
        break;
    case 0x02:
        // 400 lines
        mctl = (mctl & ~0x80) | 0x10;
        vswt = (vswt & ~0x0f) | 0x09;
        break;
    default:
        dprintf(1, "Select vert res (%02x) was discarded\n", regs->al);
        break;
    }
    SET_BDA(modeset_ctl, mctl);
    SET_BDA(video_switches, vswt);
    regs->al = 0x12;
}

static void
handle_101231(struct bregs *regs)
{
    u8 v = (regs->al & 0x01) << 3;
    u8 mctl = GET_BDA(video_ctl) & ~0x08;
    SET_BDA(video_ctl, mctl | v);
    regs->al = 0x12;
}

static void
handle_101232(struct bregs *regs)
{
    stdvga_enable_video_addressing(regs->al);
    regs->al = 0x12;
}

static void
handle_101233(struct bregs *regs)
{
    u8 v = ((regs->al << 1) & 0x02) ^ 0x02;
    u8 v2 = GET_BDA(modeset_ctl) & ~0x02;
    SET_BDA(modeset_ctl, v | v2);
    regs->al = 0x12;
}

static void
handle_101234(struct bregs *regs)
{
    u8 v = (regs->al & 0x01) ^ 0x01;
    u8 v2 = GET_BDA(modeset_ctl) & ~0x01;
    SET_BDA(modeset_ctl, v | v2);
    regs->al = 0x12;
}

static void
handle_101235(struct bregs *regs)
{
    debug_stub(regs);
    regs->al = 0x12;
}

static void
handle_101236(struct bregs *regs)
{
    debug_stub(regs);
    regs->al = 0x12;
}

static void
handle_1012XX(struct bregs *regs)
{
    debug_stub(regs);
}

static void
handle_1012(struct bregs *regs)
{
    if (CONFIG_VGA_CIRRUS && regs->bl >= 0x80) {
        clext_1012(regs);
        return;
    }

    switch (regs->bl) {
    case 0x10: handle_101210(regs); break;
    case 0x30: handle_101230(regs); break;
    case 0x31: handle_101231(regs); break;
    case 0x32: handle_101232(regs); break;
    case 0x33: handle_101233(regs); break;
    case 0x34: handle_101234(regs); break;
    case 0x35: handle_101235(regs); break;
    case 0x36: handle_101236(regs); break;
    default:   handle_1012XX(regs); break;
    }
}


// Write string
static void noinline
handle_1013(struct bregs *regs)
{
    struct cursorpos cp;
    if (regs->dh == 0xff)
        // if row=0xff special case : use current cursor position
        cp = get_cursor_pos(regs->bh);
    else
        cp = (struct cursorpos) {regs->dl, regs->dh, regs->bh};

    u16 count = regs->cx;
    u8 *offset_far = (void*)(regs->bp + 0);
    u8 attr = regs->bl;
    while (count--) {
        u8 car = GET_FARVAR(regs->es, *offset_far);
        offset_far++;
        if (regs->al & 2) {
            attr = GET_FARVAR(regs->es, *offset_far);
            offset_far++;
        }

        struct carattr ca = {car, attr, 1};
        write_teletype(&cp, ca);
    }

    if (regs->al & 1)
        set_cursor_pos(cp);
}


static void
handle_101a00(struct bregs *regs)
{
    regs->bx = GET_BDA(dcc_index);
    regs->al = 0x1a;
}

static void
handle_101a01(struct bregs *regs)
{
    SET_BDA(dcc_index, regs->bl);
    dprintf(1, "Alternate Display code (%02x) was discarded\n", regs->bh);
    regs->al = 0x1a;
}

static void
handle_101aXX(struct bregs *regs)
{
    debug_stub(regs);
}

static void
handle_101a(struct bregs *regs)
{
    switch (regs->al) {
    case 0x00: handle_101a00(regs); break;
    case 0x01: handle_101a01(regs); break;
    default:   handle_101aXX(regs); break;
    }
}


static u8 static_functionality[0x10] VAR16 = {
 /* 0 */ 0xff,  // All modes supported #1
 /* 1 */ 0xe0,  // All modes supported #2
 /* 2 */ 0x0f,  // All modes supported #3
 /* 3 */ 0x00, 0x00, 0x00, 0x00,  // reserved
 /* 7 */ 0x07,  // 200, 350, 400 scan lines
 /* 8 */ 0x02,  // mamimum number of visible charsets in text mode
 /* 9 */ 0x08,  // total number of charset blocks in text mode
 /* a */ 0xe7,  // Change to add new functions
 /* b */ 0x0c,  // Change to add new functions
 /* c */ 0x00,  // reserved
 /* d */ 0x00,  // reserved
 /* e */ 0x00,  // Change to add new functions
 /* f */ 0x00   // reserved
};

struct funcInfo {
    struct segoff_s static_functionality;
    u8 bda_0x49[30];
    u8 bda_0x84[3];
    u8 dcc_index;
    u8 dcc_alt;
    u16 colors;
    u8 pages;
    u8 scan_lines;
    u8 primary_char;
    u8 secondar_char;
    u8 misc;
    u8 non_vga_mode;
    u8 reserved_2f[2];
    u8 video_mem;
    u8 save_flags;
    u8 disp_info;
    u8 reserved_34[12];
};

static void
handle_101b(struct bregs *regs)
{
    u16 seg = regs->es;
    struct funcInfo *info = (void*)(regs->di+0);
    memset_far(seg, info, 0, sizeof(*info));
    // Address of static functionality table
    SET_FARVAR(seg, info->static_functionality
               , SEGOFF(get_global_seg(), (u32)static_functionality));

    // Hard coded copy from BIOS area. Should it be cleaner ?
    memcpy_far(seg, info->bda_0x49, SEG_BDA, (void*)0x49
               , sizeof(info->bda_0x49));
    memcpy_far(seg, info->bda_0x84, SEG_BDA, (void*)0x84
               , sizeof(info->bda_0x84));

    SET_FARVAR(seg, info->dcc_index, GET_BDA(dcc_index));
    SET_FARVAR(seg, info->colors, 16);
    SET_FARVAR(seg, info->pages, 8);
    SET_FARVAR(seg, info->scan_lines, 2);
    SET_FARVAR(seg, info->video_mem, 3);
    regs->al = 0x1B;
}


static void
handle_101c(struct bregs *regs)
{
    u16 seg = regs->es;
    void *data = (void*)(regs->bx+0);
    u16 states = regs->cx;
    if (states & ~0x07)
        goto fail;
    int ret;
    switch (regs->al) {
    case 0x00:
        ret = vgahw_size_state(states);
        if (ret < 0)
            goto fail;
        regs->bx = ret / 64;
        break;
    case 0x01:
        ret = vgahw_save_state(seg, data, states);
        if (ret)
            goto fail;
        break;
    case 0x02:
        ret = vgahw_restore_state(seg, data, states);
        if (ret)
            goto fail;
        break;
    default:
        goto fail;
    }
    regs->al = 0x1c;
fail:
    return;
}

static void
handle_10XX(struct bregs *regs)
{
    debug_stub(regs);
}

// INT 10h Video Support Service Entry Point
void VISIBLE16
handle_10(struct bregs *regs)
{
    debug_enter(regs, DEBUG_VGA_10);
    switch (regs->ah) {
    case 0x00: handle_1000(regs); break;
    case 0x01: handle_1001(regs); break;
    case 0x02: handle_1002(regs); break;
    case 0x03: handle_1003(regs); break;
    case 0x04: handle_1004(regs); break;
    case 0x05: handle_1005(regs); break;
    case 0x06: handle_1006(regs); break;
    case 0x07: handle_1007(regs); break;
    case 0x08: handle_1008(regs); break;
    case 0x09: handle_1009(regs); break;
    case 0x0a: handle_100a(regs); break;
    case 0x0b: handle_100b(regs); break;
    case 0x0c: handle_100c(regs); break;
    case 0x0d: handle_100d(regs); break;
    case 0x0e: handle_100e(regs); break;
    case 0x0f: handle_100f(regs); break;
    case 0x10: handle_1010(regs); break;
    case 0x11: handle_1011(regs); break;
    case 0x12: handle_1012(regs); break;
    case 0x13: handle_1013(regs); break;
    case 0x1a: handle_101a(regs); break;
    case 0x1b: handle_101b(regs); break;
    case 0x1c: handle_101c(regs); break;
    case 0x4f: handle_104f(regs); break;
    default:   handle_10XX(regs); break;
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
    debug_serial_setup();
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

    int ret = vgahw_init();
    if (ret) {
        dprintf(1, "Failed to initialize VGA hardware.  Exiting.\n");
        return;
    }

    if (GET_GLOBAL(HaveRunInit))
        return;

    init_bios_area();

    SET_VGA(video_save_pointer_table.videoparam
            , SEGOFF(get_global_seg(), (u32)video_param_table));
    stdvga_build_video_param();

    extern void entry_10(void);
    SET_IVT(0x10, SEGOFF(get_global_seg(), (u32)entry_10));

    SET_VGA(HaveRunInit, 1);

    // Fixup checksum
    extern u8 _rom_header_size, _rom_header_checksum;
    SET_VGA(_rom_header_checksum, 0);
    u8 sum = -checksum_far(get_global_seg(), 0,
                           GET_GLOBAL(_rom_header_size) * 512);
    SET_VGA(_rom_header_checksum, sum);
}
