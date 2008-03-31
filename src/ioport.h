// Definitions for X86 IO port access.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.
#ifndef __IOPORT_H
#define __IOPORT_H

#include "types.h" // u8

#define PORT_DMA_ADDR_2        0x0004
#define PORT_DMA_CNT_2         0x0005
#define PORT_DMA1_MASK_REG     0x000a
#define PORT_DMA1_MODE_REG     0x000b
#define PORT_DMA1_CLEAR_FF_REG 0x000c
#define PORT_DMA1_MASTER_CLEAR 0x000d
#define PORT_PIC1              0x0020
#define PORT_PIC1_DATA         0x0021
#define PORT_PIT_COUNTER0      0x0040
#define PORT_PIT_COUNTER1      0x0041
#define PORT_PIT_COUNTER2      0x0042
#define PORT_PIT_MODE          0x0043
#define PORT_PS2_DATA          0x0060
#define PORT_PS2_CTRLB         0x0061
#define PORT_PS2_STATUS        0x0064
#define PORT_CMOS_INDEX        0x0070
#define PORT_CMOS_DATA         0x0071
#define PORT_DIAG              0x0080
#define PORT_DMA_PAGE_2        0x0081
#define PORT_A20               0x0092
#define PORT_PIC2              0x00a0
#define PORT_PIC2_DATA         0x00a1
#define PORT_DMA2_MASK_REG     0x00d4
#define PORT_DMA2_MODE_REG     0x00d6
#define PORT_DMA2_MASTER_CLEAR 0x00da
#define PORT_MATH_CLEAR        0x00f0
#define PORT_FD_DOR            0x03f2
#define PORT_FD_STATUS         0x03f4
#define PORT_FD_DATA           0x03f5
#define PORT_HD_DATA           0x03f6

// PORT_PIC1 bitdefs
#define PIC1_IRQ5  (1<<5)
// PORT_PIC2 bitdefs
#define PIC2_IRQ8  (1<<0)
#define PIC2_IRQ13 (1<<5)

// PORT_KBD_CTRLB bitdefs
#define KBD_REFRESH (1<<4)


static inline void outb(u8 value, u16 port) {
    __asm__ __volatile__("outb %b0, %w1" : : "a"(value), "Nd"(port));
}
static inline void outw(u16 value, u16 port) {
    __asm__ __volatile__("outw %w0, %w1" : : "a"(value), "Nd"(port));
}
static inline void outl(u32 value, u16 port) {
    __asm__ __volatile__("outl %0, %w1" : : "a"(value), "Nd"(port));
}
static inline u8 inb(u16 port) {
    u8 value;
    __asm__ __volatile__("inb %w1, %b0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline u16 inw(u16 port) {
    u16 value;
    __asm__ __volatile__("inw %w1, %w0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline u32 inl(u16 port) {
    u32 value;
    __asm__ __volatile__("inl %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void insb(u16 port, u8 *data, u32 count) {
    asm volatile("rep insb (%%dx), %%es:(%%di)"
                 : "+c"(count), "+D"(data) : "d"(port) : "memory");
}
static inline void insw(u16 port, u16 *data, u32 count) {
    asm volatile("rep insw (%%dx), %%es:(%%di)"
                 : "+c"(count), "+D"(data) : "d"(port) : "memory");
}
static inline void insl(u16 port, u32 *data, u32 count) {
    asm volatile("rep insl (%%dx), %%es:(%%di)"
                 : "+c"(count), "+D"(data) : "d"(port) : "memory");
}
// XXX - outs not limited to es segment
static inline void outsb(u16 port, u8 *data, u32 count) {
    asm volatile("rep outsb %%es:(%%si), (%%dx)"
                 : "+c"(count), "+S"(data) : "d"(port) : "memory");
}
static inline void outsw(u16 port, u16 *data, u32 count) {
    asm volatile("rep outsw %%es:(%%si), (%%dx)"
                 : "+c"(count), "+S"(data) : "d"(port) : "memory");
}
static inline void outsl(u16 port, u32 *data, u32 count) {
    asm volatile("rep outsl %%es:(%%si), (%%dx)"
                 : "+c"(count), "+S"(data) : "d"(port) : "memory");
}

#endif // ioport.h
