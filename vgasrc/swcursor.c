// Virtual software based cursor support
//
// Copyright (C) 2014-2016  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "biosvar.h" // GET_BDA
#include "vgabios.h" // handle_gfx_op

// Draw/undraw a cursor on the framebuffer by xor'ing the cursor cell
static void
gfx_set_swcursor(struct vgamode_s *vmode_g, int enable, struct cursorpos cp)
{
    u16 cursor_type = get_cursor_shape();
    u8 start = cursor_type >> 8, end = cursor_type & 0xff;
    struct gfx_op op;
    init_gfx_op(&op, vmode_g);
    op.x = cp.x * 8;
    int cheight = GET_BDA(char_height);
    op.y = cp.y * cheight + start;

    int i;
    for (i = start; i < cheight && i <= end; i++, op.y++) {
        op.op = GO_READ8;
        handle_gfx_op(&op);
        int j;
        for (j = 0; j < 8; j++)
            op.pixels[j] ^= 0x07;
        op.op = GO_WRITE8;
        handle_gfx_op(&op);
    }
}

// Draw/undraw a cursor on the screen
void
vgafb_set_swcursor(int enable)
{
    if (!vga_emulate_text())
        return;
    u8 flags = GET_BDA_EXT(flags);
    if (!!(flags & BF_SWCURSOR) == enable)
        // Already in requested mode.
        return;
    struct vgamode_s *vmode_g = get_current_mode();
    if (!vmode_g)
        return;
    struct cursorpos cp = get_cursor_pos(GET_BDA(video_page));
    if (cp.x >= GET_BDA(video_cols) || cp.y > GET_BDA(video_rows)
        || GET_BDA(cursor_type) >= 0x2000)
        // Cursor not visible
        return;

    SET_BDA_EXT(flags, (flags & ~BF_SWCURSOR) | (enable ? BF_SWCURSOR : 0));

    if (GET_GLOBAL(vmode_g->memmodel) != MM_TEXT) {
        gfx_set_swcursor(vmode_g, enable, cp);
        return;
    }

    // In text mode, swap foreground and background attributes for cursor
    void *dest_far = text_address(cp) + 1;
    u8 attr = GET_FARVAR(GET_GLOBAL(vmode_g->sstart), *(u8*)dest_far);
    attr = (attr >> 4) | (attr << 4);
    SET_FARVAR(GET_GLOBAL(vmode_g->sstart), *(u8*)dest_far, attr);
}
