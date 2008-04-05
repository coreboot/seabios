// Rom layout and bios assembler to C interface.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "config.h"

#define PROTECTED_MODE_CS (2 << 3) // 0x10
#define PROTECTED_MODE_DS (3 << 3) // 0x18
#define REAL_MODE_CS      (4 << 3) // 0x20
#define REAL_MODE_DS      (5 << 3) // 0x28

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
 * Entry macros
 ****************************************************************/

        // Call a C function - this does the minimal work necessary to
        // call into C.  It sets up %ds, backs up %es, and backs up
        // those registers that are call clobbered by the C compiler.
        .macro ENTRY cfunc
        cld
        pushl %eax              // Save registers clobbered by C code
        pushl %ecx
        pushl %edx
        pushw %es
        pushw %ds
        movw %ss, %ax           // Move %ss to %ds
        movw %ax, %ds
        pushl %esp              // Backup %esp, then clear high bits
        movzwl %sp, %esp
        calll \cfunc
        popl %esp               // Restore %esp (including high bits)
        popw %ds                // Restore registers saved above
        popw %es
        popl %edx
        popl %ecx
        popl %eax
        .endm

        // Call a C function with current register list as an
        // argument.  This backs up the registers and sets %eax
        // to point to the backup.  On return, the registers are
        // restored from the structure.
        .macro ENTRY_ARG cfunc
        cld
        pushl %eax              // Save registers (matches struct bregs)
        pushl %ecx
        pushl %edx
        pushl %ebx
        pushl %esi
        pushl %edi
        pushw %es
        pushw %ds
        movw %ss, %ax           // Move %ss to %ds
        movw %ax, %ds
        movl %esp, %ebx         // Backup %esp, then zero high bits
        movzwl %sp, %esp
        movl %esp, %eax         // First arg is pointer to struct bregs
        calll \cfunc
        movl %ebx, %esp         // Restore %esp (including high bits)
        popw %ds                // Restore registers (from struct bregs)
        popw %es
        popl %edi
        popl %esi
        popl %ebx
        popl %edx
        popl %ecx
        popl %eax
        .endm

        // Macro to reset the 16bit stack
        // Clobbers %ax
        .macro RESET_STACK
        xorw %ax, %ax
        movw %ax, %ss
        movl $ CONFIG_STACK_OFFSET , %esp
        .endm


/****************************************************************
 * POST handler
 ****************************************************************/

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

        cld

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
        // ljmpl $PROTECTED_MODE_CS, $(1f | 0xf0000)
        .word 0xea66, 1f, 0x000f, PROTECTED_MODE_CS

        .code32
1:
        // init data segments
        movl $PROTECTED_MODE_DS, %eax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        xorl %eax, %eax
        movw %ax, %fs
        movw %ax, %gs

        retl

// Call a 16bit function from 32bit mode.
// %eax = address of struct bregs
// Clobbers: all gp registers, flags, stack registers, cr0, idt/gdt
        .globl __call16_from32
__call16_from32:
        pushl %eax

        // Jump to 16bit mode
        ljmpw $REAL_MODE_CS, $1f

        .code16gcc
1:
        // restore data segment limits to 0xffff
        movw $REAL_MODE_DS, %ax
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

        // Set __call16 return address to be transition32
        pushl $transition32

        // Fall through to __call16


// Call a 16bit function from 16bit mode with a specified cpu register state
// %eax = address of struct bregs
// Clobbers: all gp registers, es
        .globl __call16
__call16:
        // Save eax
        pushl %eax

        // Setup for iretw call
        pushw $0xf000
        pushw $1f               // return point
        pushw 0x20(%eax)        // flags
        pushl 0x1c(%eax)        // CS:IP

        // Load calling registers.
        movl 0x04(%eax), %edi
        movl 0x08(%eax), %esi
        movl 0x0c(%eax), %ebx
        movl 0x10(%eax), %edx
        movl 0x14(%eax), %ecx
        movw 0x02(%eax), %es    // XXX - should load %ds too
        movl 0x18(%eax), %eax

        // Invoke call
        iretw                   // XXX - just do a lcalll
1:
        // Store flags, eax, ecx
        pushfw
        pushl %eax
        movl 0x06(%esp), %eax
        movl %ecx, %ss:0x14(%eax)       // Save %ecx
        movw %ss, %cx
        movw %cx, %ds                   // Restore %ds == %ss
        popl %ecx
        movl %ecx, 0x18(%eax)           // Save %eax
        popw %cx
        movw %cx, 0x20(%eax)            // Save flags

        // Store remaining registers
        movw %es, 0x02(%eax)
        movl %edi, 0x04(%eax)
        movl %esi, 0x08(%eax)
        movl %ebx, 0x0c(%eax)
        movl %edx, 0x10(%eax)

        // Remove %eax
        popl %eax

        cld

        retl


// APM trampolines
        .globl apm16protected_entry
apm16protected_entry:
        pushfw          // save flags
        pushl %eax      // dummy
        ENTRY_ARG handle_1553
        addw $4, %sp    // pop dummy
        popfw           // restore flags
        lretw

        .code32
        .globl apm32protected_entry
apm32protected_entry:
        pushfw
        pushw %cs       // Setup for long jump to 16bit mode
        pushw $1f
        incw 2(%esp)
        ljmpw *(%esp)
        .code16gcc
1:
        ENTRY_ARG handle_1553

        movw $2f,(%esp) // Setup for long jump back to 32bit mode
        decw 2(%esp)
        ljmpw *(%esp)
        .code32
2:
        addl $4, %esp   // pop call address
        popfw
        lretl
        .code16gcc


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
        .long 0xf0000 // base 16:47

// Real mode IDT descriptor
//
// Set to typical real-mode values.
// base  = 000000
// limit =   03ff
rmode_IDT_info:
        .word 0x03ff  // limit 15:00
        .long 0       // base 16:47

rombios32_gdt_48:
        .word 0x30
        .word rombios32_gdt
        .word 0x000f

        .balign 8
rombios32_gdt:
        .word 0, 0, 0, 0
        .word 0, 0, 0, 0
        // 32 bit flat code segment (PROTECTED_MODE_CS)
        .word 0xffff, 0, 0x9b00, 0x00cf
        // 32 bit flat data segment (PROTECTED_MODE_DS)
        .word 0xffff, 0, 0x9300, 0x00cf
        // 16 bit code segment base=0xf0000 limit=0xffff (REAL_MODE_CS)
        .word 0xffff, 0, 0x9b0f, 0x0000
        // 16 bit data segment base=0x0 limit=0xffff (REAL_MODE_DS)
        .word 0xffff, 0, 0x9300, 0x0000


/****************************************************************
 * Interrupt entry points
 ****************************************************************/

        // Define an entry point for an interrupt (no args passed).
        .macro IRQ_ENTRY num
        .globl entry_\num
        entry_\num :
        cli         // In case something far-calls instead of using "int"
        ENTRY handle_\num
        iretw
        .endm

        // Define an entry point for an interrupt (can read/modify args).
        .macro IRQ_ENTRY_ARG num
        .globl entry_\num
        entry_\num :
        cli         // In case something far-calls instead of using "int"
        ENTRY_ARG handle_\num
        iretw
        .endm

        .org 0xe2c3
        IRQ_ENTRY nmi

        IRQ_ENTRY_ARG 13
        IRQ_ENTRY_ARG 12
        IRQ_ENTRY_ARG 11
        IRQ_ENTRY 76
        IRQ_ENTRY 1c
        IRQ_ENTRY 70

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
        IRQ_ENTRY_ARG 14

        IRQ_ENTRY 74
        IRQ_ENTRY 75

        // int 18/19 are special - they reset the stack and do not return.
        .globl entry_19
entry_19:
        RESET_STACK
        ENTRY handle_19

        .globl entry_18
entry_18:
        RESET_STACK
        ENTRY handle_18

        // IRQ trampolines
        .macro IRQ_TRAMPOLINE num
        .globl irq_trampoline_0x\num
        irq_trampoline_0x\num :
        int $0x\num
        lretw
        .endm

        IRQ_TRAMPOLINE 02
        IRQ_TRAMPOLINE 10
        IRQ_TRAMPOLINE 13
        IRQ_TRAMPOLINE 15
        IRQ_TRAMPOLINE 16
        IRQ_TRAMPOLINE 18
        IRQ_TRAMPOLINE 19
        IRQ_TRAMPOLINE 1c
        IRQ_TRAMPOLINE 4a

        .org 0xe82e
        IRQ_ENTRY_ARG 16

        .org 0xe987
        IRQ_ENTRY 09

        .org 0xec59
        IRQ_ENTRY_ARG 40

        .org 0xef57
        IRQ_ENTRY 0e

        .org 0xefc7
.include "out/floppy_dbt.proc.16.s"
        .text

        .org 0xefd2
        IRQ_ENTRY_ARG 17

        .org 0xf045
        // XXX int 10
        iretw

        .org 0xf065
        IRQ_ENTRY_ARG 10

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
        IRQ_ENTRY_ARG 15

        .org 0xfa6e
.include "out/font.proc.16.s"
        .text

        .org 0xfe6e
        IRQ_ENTRY_ARG 1a

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
        IRQ_ENTRY_ARG 05

        .org 0xfff0 // Power-up Entry Point
        ljmpw $0xf000, $post16

        .org 0xfff5
        // BIOS build date
        .ascii "06/23/99"

        .org 0xfffe
        .byte CONFIG_MODEL_ID
        .byte 0x00

        .end
