// Helpers for working with i8259 interrupt controller.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "pic.h" // get_pic1_isr
#include "util.h" // dprintf
#include "config.h" // CONFIG_*

void
pic_setup()
{
    dprintf(3, "init pic\n");
    // Send ICW1 (select OCW1 + will send ICW4)
    outb(0x11, PORT_PIC1_CMD);
    outb(0x11, PORT_PIC2_CMD);
    // Send ICW2 (base irqs: 0x08-0x0f for irq0-7, 0x70-0x77 for irq8-15)
    outb(0x08, PORT_PIC1_DATA);
    outb(0x70, PORT_PIC2_DATA);
    // Send ICW3 (cascaded pic ids)
    outb(0x04, PORT_PIC1_DATA);
    outb(0x02, PORT_PIC2_DATA);
    // Send ICW4 (enable 8086 mode)
    outb(0x01, PORT_PIC1_DATA);
    outb(0x01, PORT_PIC2_DATA);
    // Mask all irqs (except cascaded PIC2 irq)
    outb(~PIC1_IRQ2, PORT_PIC1_DATA);
    outb(~0, PORT_PIC2_DATA);
}

// Handler for otherwise unused hardware irqs.
void VISIBLE16
handle_hwirq(struct bregs *regs)
{
    debug_isr(DEBUG_ISR_hwirq);

    u8 isr1 = get_pic1_isr();
    if (! isr1) {
        dprintf(1, "Got hwirq with no ISR\n");
        return;
    }

    u8 isr2 = get_pic2_isr();
    u16 isr = isr2<<8 | isr1;
    dprintf(1, "Masking noisy irq %x\n", isr);
    if (isr2) {
        mask_pic2(isr2);
        eoi_pic2();
    } else {
        if (! (isr1 & 0x2)) // don't ever mask the cascaded irq
            mask_pic1(isr1);
        eoi_pic1();
    }
}
