// Rom layout and bios assembler to C interface.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "config.h"

        .code16gcc
        .text
        .globl bios16c_start, bios16c_end
bios16c_start:
.include "out/blob.proc.16.s"
        .text
bios16c_end:


        .org 0xe05b
        .globl _start
_start:
        .globl post16
post16:

        // Set entry point of rombios32 code - the actual instruction
	// is altered later in the build process.
        .globl set_entry32
set_entry32:
        mov $0xf0000000, %ebx

        // init the stack pointer
        movl $ CONFIG_STACK32_OFFSET , %esp

transition32:
        // Disable irqs
        cli

        // enable a20
        inb $0x92, %al
        orb $0x02, %al
        outb %al, $0x92

        // Set segment descriptors
        lidt %cs:pmode_IDT_info
        lgdt %cs:rombios32_gdt_48

        // set PE bit in CR0
        movl  %cr0, %eax
        orb   $0x01, %al
        movl  %eax, %cr0

        // start protected mode code
        .word 0xea66, 1f, 0x000f, 0x0010 // ljmpl $0x10, $(1f | 0xf0000)

        .code32
1:
        // init data segments
        movl $0x18, %eax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        xorl %eax, %eax
        movw %ax, %fs
        movw %ax, %gs

        cld

        jmp *%ebx

        .code16gcc

// We need a copy of this string, but we are not actually a PnP BIOS,
// so make sure it is *not* aligned, so OSes will not see it if they
// scan.
        .align 2
        .byte 0
pnp_string:
        .ascii "$PnP"

// Return from 32bit code to 16bit code - must pass in destination
// code segment,offset (%ebx) and the return stack position (%esp).

        .globl call16
call16:
        // restore data segment limits to 0xffff
        movw $0x28, %ax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        movw %ax, %fs
        movw %ax, %gs

        // reset PE bit in CR0
        movl %cr0, %eax
        andb $0xfe, %al
        movl %eax, %cr0

        // far jump to flush CPU queue after transition to real mode
        ljmpw $0xf000, $1f
1:
        // restore IDT to normal real-mode defaults
        lidt %cs:rmode_IDT_info

        // Setup segment registers
        xorw %ax, %ax
        movw %ax, %ds
        movw %ax, %fs
        movw %ax, %gs
        movw $0xf000, %ax
        movw %ax, %es
        lea pnp_string, %di
        movw $ CONFIG_STACK16_SEGMENT , %ax
        movw %ax, %ss
        movl %esp, %eax
        movl $ CONFIG_STACK16_OFFSET , %esp

        // Save info
        pushl %eax
        pushl %ebx
        movl %esp, %ebp

        lcallw %ss:*(%bp)

        // Restore stack and jump back to 32bit mode.
        popl %eax
        popl %esp

        // Set resume point of rombios32 code - the actual instruction
	// is altered later in the build process.
        .globl set_resume32
set_resume32:
        mov $0xf0000000, %ebx

        jmp transition32


// Protected mode IDT descriptor
//
// I just make the limit 0, so the machine will shutdown
// if an exception occurs during protected mode memory
// transfers.
//
// Set base to f0000 to correspond to beginning of BIOS,
// in case I actually define an IDT later
// Set limit to 0
pmode_IDT_info:
        .word 0x0000  // limit 15:00
        .word 0x0000  // base  15:00
        .byte 0x0f    // base  23:16

// Real mode IDT descriptor
//
// Set to typical real-mode values.
// base  = 000000
// limit =   03ff
rmode_IDT_info:
        .word 0x03ff  // limit 15:00
        .word 0x0000  // base  15:00
        .byte 0x00    // base  23:16

rombios32_gdt_48:
        .word 0x30
        .word rombios32_gdt
        .word 0x000f

rombios32_gdt:
        .word 0, 0, 0, 0
        .word 0, 0, 0, 0
        .word 0xffff, 0, 0x9b00, 0x00cf // 32 bit flat code segment (0x10)
        .word 0xffff, 0, 0x9300, 0x00cf // 32 bit flat data segment (0x18)
        .word 0xffff, 0, 0x9b0f, 0x0000 // 16 bit code segment base=0xf0000 limit=0xffff
        .word 0xffff, 0, 0x9300, 0x0000 // 16 bit data segment base=0x0 limit=0xffff


        .macro ENTRY cfunc
        pushal
        pushw %es
        pushw %ds
        movw %ss, %ax
        movw %ax, %ds
        mov %esp, %eax
        call \cfunc
        popw %ds
        popw %es
        popal
        .endm

        .macro IRQ_ENTRY num
        .globl entry_\num
        entry_\num :
        ENTRY handle_\num
        iretw
        .endm


        .org 0xe2c3
        IRQ_ENTRY nmi

        IRQ_ENTRY 13
        IRQ_ENTRY 19
        IRQ_ENTRY 12
        IRQ_ENTRY 11
        IRQ_ENTRY 76
        IRQ_ENTRY 18
        IRQ_ENTRY 1c
        IRQ_ENTRY 70
        IRQ_ENTRY 74
        IRQ_ENTRY 75

        .org 0xe3fe
        jmp entry_13

        .org 0xe401
        // XXX - Fixed Disk Parameter Table

        .org 0xe6f2
        jmp entry_19

        .org 0xe6f5
.include "out/cbt.proc.16.s"
        .text

        .org 0xe729
        // XXX - Baud Rate Generator Table

        .org 0xe739
        IRQ_ENTRY 14

        .org 0xe82e
        IRQ_ENTRY 16

        .org 0xe987
        IRQ_ENTRY 09

        .org 0xec59
        IRQ_ENTRY 40

        .org 0xef57
        IRQ_ENTRY 0e

        .org 0xefc7
        // XXX - Diskette Controller Parameter Table

        .org 0xefd2
        IRQ_ENTRY 17

        .org 0xf045
        // XXX int 10
        iretw

        .org 0xf065
        IRQ_ENTRY 10

        .org 0xf0a4
        // XXX int 1D
        iretw

        .org 0xf841
        jmp entry_12

        .org 0xf84d
        jmp entry_11

        .org 0xf859
        IRQ_ENTRY 15

        .org 0xfa6e
.include "out/font.proc.16.s"
        .text

        .org 0xfe6e
        IRQ_ENTRY 1a

        .org 0xfea5
        IRQ_ENTRY 08

        .org 0xfef3
        // XXX - Initial Interrupt Vector Offsets Loaded by POST

        .org 0xff00
        // XXX - BIOS_COPYRIGHT_STRING
        .ascii "(c) 2002 MandrakeSoft S.A. Written by Kevin Lawton & the Bochs team."

        .org 0xff53
        .globl dummy_iret_handler
dummy_iret_handler:
        iretw

        .org 0xff54
        IRQ_ENTRY 05

        .org 0xfff0 // Power-up Entry Point
        ljmpw $0xf000, $post16

        .org 0xfff5
        // BIOS build date
        .ascii "06/23/99"

        .org 0xfffe
        // model byte 0xFC = AT
        .byte 0xfc
        .byte 0x00

        .end
