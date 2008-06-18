// Helpers for working with i8259 interrupt controller.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.
#ifndef __PIC_H
#define __PIC_H

#include "ioport.h" // PORT_PIC*
#include "util.h" // dprintf

// PORT_PIC1 bitdefs
#define PIC1_IRQ0  (1<<0)
#define PIC1_IRQ1  (1<<1)
#define PIC1_IRQ2  (1<<2)
#define PIC1_IRQ5  (1<<5)
#define PIC1_IRQ6  (1<<6)
// PORT_PIC2 bitdefs
#define PIC2_IRQ8  (1<<0)
#define PIC2_IRQ12 (1<<4)
#define PIC2_IRQ13 (1<<5)
#define PIC2_IRQ14 (1<<6)

static inline void
eoi_pic1()
{
    // Send eoi (select OCW2 + eoi)
    outb(0x20, PORT_PIC1_CMD);
}

static inline void
eoi_pic2()
{
    // Send eoi (select OCW2 + eoi)
    outb(0x20, PORT_PIC2_CMD);
    eoi_pic1();
}

static inline void
unmask_pic1(u8 irq)
{
    outb(inb(PORT_PIC1_DATA) & ~irq, PORT_PIC1_DATA);
}

static inline void
unmask_pic2(u8 irq)
{
    outb(inb(PORT_PIC2_DATA) & ~irq, PORT_PIC2_DATA);
}

static inline u8
get_pic1_isr()
{
    // 0x0b == select OCW1 + read ISR
    outb(0x0b, PORT_PIC1_CMD);
    return inb(PORT_PIC1_CMD);
}

static inline void
pic_setup()
{
    dprintf(3, "init pic\n");
    // Send ICW1 (select OCW1 + will send ICW4)
    outb(0x11, PORT_PIC1_CMD);
    outb(0x11, PORT_PIC2_CMD);
    // Send ICW2 (base irqs: 0x08-0x0f for irq0-7, 0x70-0x78 for irq8-15)
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

#endif // pic.h
