// VGA io port access
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2001-2008 the LGPL VGABios developers Team
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "ioport.h" // outb
#include "bregs.h" // struct bregs
#include "farptr.h" // SET_FARVAR
#include "vgatables.h" // VGAREG_*


/****************************************************************
 * Attribute control
 ****************************************************************/

void
biosfn_set_border_color(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);
    outb(0x00, VGAREG_ACTL_ADDRESS);
    u8 al = regs->bl & 0x0f;
    if (al & 0x08)
        al += 0x08;
    outb(al, VGAREG_ACTL_WRITE_DATA);
    u8 bl = regs->bl & 0x10;

    int i;
    for (i = 1; i < 4; i++) {
        outb(i, VGAREG_ACTL_ADDRESS);

        al = inb(VGAREG_ACTL_READ_DATA);
        al &= 0xef;
        al |= bl;
        outb(al, VGAREG_ACTL_WRITE_DATA);
    }
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

void
biosfn_set_overscan_border_color(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);
    outb(0x11, VGAREG_ACTL_ADDRESS);
    outb(regs->bh, VGAREG_ACTL_WRITE_DATA);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

void
biosfn_read_overscan_border_color(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);
    outb(0x11, VGAREG_ACTL_ADDRESS);
    regs->bh = inb(VGAREG_ACTL_READ_DATA);
    inb(VGAREG_ACTL_RESET);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

void
biosfn_set_palette(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);
    u8 bl = regs->bl & 0x01;
    int i;
    for (i = 1; i < 4; i++) {
        outb(i, VGAREG_ACTL_ADDRESS);

        u8 al = inb(VGAREG_ACTL_READ_DATA);
        al &= 0xfe;
        al |= bl;
        outb(al, VGAREG_ACTL_WRITE_DATA);
    }
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

void
biosfn_set_single_palette_reg(u8 reg, u8 val)
{
    inb(VGAREG_ACTL_RESET);
    outb(reg, VGAREG_ACTL_ADDRESS);
    outb(val, VGAREG_ACTL_WRITE_DATA);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

u8
biosfn_get_single_palette_reg(u8 reg)
{
    inb(VGAREG_ACTL_RESET);
    outb(reg, VGAREG_ACTL_ADDRESS);
    u8 v = inb(VGAREG_ACTL_READ_DATA);
    inb(VGAREG_ACTL_RESET);
    outb(0x20, VGAREG_ACTL_ADDRESS);
    return v;
}

void
biosfn_set_all_palette_reg(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);

    u8 *data_far = (u8*)(regs->dx + 0);
    int i;
    for (i = 0; i < 0x10; i++) {
        outb(i, VGAREG_ACTL_ADDRESS);
        u8 val = GET_FARVAR(regs->es, *data_far);
        outb(val, VGAREG_ACTL_WRITE_DATA);
        data_far++;
    }
    outb(0x11, VGAREG_ACTL_ADDRESS);
    outb(GET_FARVAR(regs->es, *data_far), VGAREG_ACTL_WRITE_DATA);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

void
biosfn_get_all_palette_reg(struct bregs *regs)
{
    u8 *data_far = (u8*)(regs->dx + 0);
    int i;
    for (i = 0; i < 0x10; i++) {
        inb(VGAREG_ACTL_RESET);
        outb(i, VGAREG_ACTL_ADDRESS);
        SET_FARVAR(regs->es, *data_far, inb(VGAREG_ACTL_READ_DATA));
        data_far++;
    }
    inb(VGAREG_ACTL_RESET);
    outb(0x11, VGAREG_ACTL_ADDRESS);
    SET_FARVAR(regs->es, *data_far, inb(VGAREG_ACTL_READ_DATA));
    inb(VGAREG_ACTL_RESET);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

void
biosfn_toggle_intensity(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);
    outb(0x10, VGAREG_ACTL_ADDRESS);
    u8 val = (inb(VGAREG_ACTL_READ_DATA) & 0x7f) | ((regs->bl & 0x01) << 3);
    outb(val, VGAREG_ACTL_WRITE_DATA);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

void
biosfn_select_video_dac_color_page(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);
    outb(0x10, VGAREG_ACTL_ADDRESS);
    u8 val = inb(VGAREG_ACTL_READ_DATA);
    if (!(regs->bl & 0x01)) {
        val = (val & 0x7f) | (regs->bh << 7);
        outb(val, VGAREG_ACTL_WRITE_DATA);
        outb(0x20, VGAREG_ACTL_ADDRESS);
        return;
    }
    inb(VGAREG_ACTL_RESET);
    outb(0x14, VGAREG_ACTL_ADDRESS);
    u8 bh = regs->bh;
    if (!(val & 0x80))
        bh <<= 2;
    bh &= 0x0f;
    outb(bh, VGAREG_ACTL_WRITE_DATA);
    outb(0x20, VGAREG_ACTL_ADDRESS);
}

void
biosfn_read_video_dac_state(struct bregs *regs)
{
    inb(VGAREG_ACTL_RESET);
    outb(0x10, VGAREG_ACTL_ADDRESS);
    u8 val1 = inb(VGAREG_ACTL_READ_DATA) >> 7;

    inb(VGAREG_ACTL_RESET);
    outb(0x14, VGAREG_ACTL_ADDRESS);
    u8 val2 = inb(VGAREG_ACTL_READ_DATA) & 0x0f;
    if (!(val1 & 0x01))
        val2 >>= 2;

    inb(VGAREG_ACTL_RESET);
    outb(0x20, VGAREG_ACTL_ADDRESS);

    regs->bl = val1;
    regs->bh = val2;
}


/****************************************************************
 * DAC control
 ****************************************************************/

void
biosfn_set_single_dac_reg(struct bregs *regs)
{
    outb(regs->bl, VGAREG_DAC_WRITE_ADDRESS);
    outb(regs->dh, VGAREG_DAC_DATA);
    outb(regs->ch, VGAREG_DAC_DATA);
    outb(regs->cl, VGAREG_DAC_DATA);
}

void
biosfn_read_single_dac_reg(struct bregs *regs)
{
    outb(regs->bl, VGAREG_DAC_READ_ADDRESS);
    regs->dh = inb(VGAREG_DAC_DATA);
    regs->ch = inb(VGAREG_DAC_DATA);
    regs->cl = inb(VGAREG_DAC_DATA);
}

void
biosfn_set_all_dac_reg(struct bregs *regs)
{
    outb(regs->bl, VGAREG_DAC_WRITE_ADDRESS);
    u8 *data_far = (u8*)(regs->dx + 0);
    int count = regs->cx;
    while (count) {
        outb(GET_FARVAR(regs->es, *data_far), VGAREG_DAC_DATA);
        data_far++;
        outb(GET_FARVAR(regs->es, *data_far), VGAREG_DAC_DATA);
        data_far++;
        outb(GET_FARVAR(regs->es, *data_far), VGAREG_DAC_DATA);
        data_far++;
        count--;
    }
}

void
biosfn_read_all_dac_reg(struct bregs *regs)
{
    outb(regs->bl, VGAREG_DAC_READ_ADDRESS);
    u8 *data_far = (u8*)(regs->dx + 0);
    int count = regs->cx;
    while (count) {
        SET_FARVAR(regs->es, *data_far, inb(VGAREG_DAC_DATA));
        data_far++;
        SET_FARVAR(regs->es, *data_far, inb(VGAREG_DAC_DATA));
        data_far++;
        SET_FARVAR(regs->es, *data_far, inb(VGAREG_DAC_DATA));
        data_far++;
        count--;
    }
}

void
biosfn_set_pel_mask(struct bregs *regs)
{
    outb(regs->bl, VGAREG_PEL_MASK);
}

void
biosfn_read_pel_mask(struct bregs *regs)
{
    regs->bl = inb(VGAREG_PEL_MASK);
}


/****************************************************************
 * Memory control
 ****************************************************************/

void
biosfn_set_text_block_specifier(struct bregs *regs)
{
    outw((regs->bl << 8) | 0x03, VGAREG_SEQU_ADDRESS);
}

void
get_font_access()
{
    outw(0x0100, VGAREG_SEQU_ADDRESS);
    outw(0x0402, VGAREG_SEQU_ADDRESS);
    outw(0x0704, VGAREG_SEQU_ADDRESS);
    outw(0x0300, VGAREG_SEQU_ADDRESS);
    outw(0x0204, VGAREG_GRDC_ADDRESS);
    outw(0x0005, VGAREG_GRDC_ADDRESS);
    outw(0x0406, VGAREG_GRDC_ADDRESS);
}

void
release_font_access()
{
    outw(0x0100, VGAREG_SEQU_ADDRESS);
    outw(0x0302, VGAREG_SEQU_ADDRESS);
    outw(0x0304, VGAREG_SEQU_ADDRESS);
    outw(0x0300, VGAREG_SEQU_ADDRESS);
    u16 v = (inw(VGAREG_READ_MISC_OUTPUT) & 0x01) ? 0x0e : 0x0a;
    outw((v << 8) | 0x06, VGAREG_GRDC_ADDRESS);
    outw(0x0004, VGAREG_GRDC_ADDRESS);
    outw(0x1005, VGAREG_GRDC_ADDRESS);
}


/****************************************************************
 * Misc
 ****************************************************************/

void
biosfn_enable_video_addressing(struct bregs *regs)
{
    u8 v = ((regs->al << 1) & 0x02) ^ 0x02;
    u8 v2 = inb(VGAREG_READ_MISC_OUTPUT) & ~0x02;
    outb(v | v2, VGAREG_WRITE_MISC_OUTPUT);
    regs->ax = 0x1212;
}

void
init_vga_card()
{
    // switch to color mode and enable CPU access 480 lines
    outb(0xc3, VGAREG_WRITE_MISC_OUTPUT);
    // more than 64k 3C4/04
    outb(0x04, VGAREG_SEQU_ADDRESS);
    outb(0x02, VGAREG_SEQU_DATA);
}
