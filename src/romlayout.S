// Rom layout and bios assembler to C interface.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "config.h"

        .code16gcc


/****************************************************************
 * Include of 16bit C code
 ****************************************************************/

        .globl bios16c_start, bios16c_end
bios16c_start:
.include "out/blob.proc.16.s"
        .text
bios16c_end:


/****************************************************************
 * POST handler
 ****************************************************************/

        // Macro to reset the 16bit stack
        // Clobbers %ax
        .macro RESET_STACK
        xorw %ax, %ax
        movw %ax, %ss
        movl $ CONFIG_STACK_OFFSET , %esp
        .endm

        .org 0xe05b
        .globl post16
post16:
        // init the stack pointer
        RESET_STACK

        // Set entry point of rombios32 code - the actual address
	// is altered later in the build process.
        .globl set_entry32
set_entry32:
        pushl $0xf0000000

        // Fall through to transition32 function below


/****************************************************************
 * Call trampolines
 ****************************************************************/

// Place CPU into 32bit mode from 16bit mode.
// Clobbers: %eax, flags, stack registers, cr0, idt/gdt
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

        retl

// Call a 16bit function from 32bit mode.
// 4(%esp) = address of struct bregs
// Clobbers: all gp registers, flags, stack registers, cr0, idt/gdt
        .globl __call16_from32
__call16_from32:
        pushl %eax

        // Jump to 16bit mode
        ljmp $0x20, $1f

        .code16gcc
1:
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
        ljmpw $0xf000, $2f
2:
        // restore IDT to normal real-mode defaults
        lidt %cs:rmode_IDT_info

        // Clear segment registers
        xorw %ax, %ax
        movw %ax, %fs
        movw %ax, %gs
        movw %ax, %es
        movw %ax, %ds
        movw %ax, %ss  // Assume stack is in segment 0

        popl %eax
        pushl $transition32

        // Fall through to __call16


// Call a 16bit function with a specified cpu register state
// %eax = address of struct bregs
// Clobbers: all gp registers, es
        .globl __call16
__call16:
        // Save eax
        pushl %eax

        // Setup for iretw call
        pushw $0xf000
        pushw $1f               // return point
        pushw 0x28(%eax)        // flags
        pushl 0x24(%eax)        // CS:IP

        // Load calling registers.
        movl 0x04(%eax), %edi
        movl 0x08(%eax), %esi
        movl 0x0c(%eax), %ebp
        movl 0x14(%eax), %ebx
        movl 0x18(%eax), %edx
        movl 0x1c(%eax), %ecx
        movw 0x02(%eax), %es    // XXX - should load %ds too
        movl 0x20(%eax), %eax

        // Invoke call
        iretw                   // XXX - just do a lcalll
1:
        // Store flags, eax, ecx
        pushfw
        pushl %eax
        movl 0x06(%esp), %eax
        movl %ecx, 0x1c(%eax)   // Save %ecx
        popl %ecx
        movl %ecx, 0x20(%eax)   // Save %eax
        popw %cx
        movw %cx, 0x28(%eax)    // Save flags

        // Store remaining registers
        movw %es, 0x02(%eax)
        movl %edi, 0x04(%eax)
        movl %esi, 0x08(%eax)
        movl %ebp, 0x0c(%eax)
        movl %ebx, 0x14(%eax)
        movl %edx, 0x18(%eax)

        // Remove %eax
        popl %eax

        retl


/****************************************************************
 * GDT and IDT tables
 ****************************************************************/

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

// We need a copy of this string, but we are not actually a PnP BIOS,
// so make sure it is *not* aligned, so OSes will not see it if they
// scan.
        .align 2
        .byte 0
        .globl pnp_string
pnp_string:
        .ascii "$PnP"


/****************************************************************
 * Interrupt entry points
 ****************************************************************/

        .macro ENTRY cfunc
        cli         // In case something far-calls insted of using "int"
        pushal
        pushw %es
        pushw %ds
        movw %ss, %ax
        movw %ax, %ds
        movzwl %sp, %esp
        movl %esp, %eax
        calll \cfunc
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

        .macro IRQ_TRAMPOLINE num
        .globl irq_trampoline_0x\num
        irq_trampoline_0x\num :
        int $0x\num
        lretw
        .endm

        .org 0xe2c3
        IRQ_ENTRY nmi

        IRQ_ENTRY 13
        IRQ_ENTRY 12
        IRQ_ENTRY 11
        IRQ_ENTRY 76
        IRQ_ENTRY 1c
        IRQ_ENTRY 70
        IRQ_ENTRY 74
        IRQ_ENTRY 75

        .globl entry_19
entry_19:
        RESET_STACK
        calll handle_19

        .globl entry_18
entry_18:
        RESET_STACK
        calll handle_18

        IRQ_TRAMPOLINE 02
        IRQ_TRAMPOLINE 10
        IRQ_TRAMPOLINE 13
        IRQ_TRAMPOLINE 15
        IRQ_TRAMPOLINE 18
        IRQ_TRAMPOLINE 19
        IRQ_TRAMPOLINE 1c
        IRQ_TRAMPOLINE 4a

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

        .globl freespace2_start, freespace2_end
freespace2_start:

        .org 0xf841
freespace2_end:
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
