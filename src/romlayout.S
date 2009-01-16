// Rom layout and bios assembler to C interface.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "config.h" // CONFIG_*
#include "ioport.h" // PORT_A20
#include "bregs.h" // CR0_*
#include "cmos.h" // CMOS_RESET_CODE
#include "../out/asm-offsets.h" // BREGS_*


/****************************************************************
 * Include of 16bit C code
 ****************************************************************/

        .code16gcc
.include "out/ccode.16.s"


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

        // As above, but don't mangle %esp
        .macro ENTRY_ARG_ESP cfunc
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
        movl %esp, %eax         // First arg is pointer to struct bregs
        calll \cfunc
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
        movl $ BUILD_STACK_ADDR , %esp
        cld
        .endm

        // Specify a location in the fixed part of bios area.
        .macro ORG addr
        .section .text.fixed.addr
        .org \addr - BUILD_START_FIXED
        .endm


/****************************************************************
 * POST handler
 ****************************************************************/

        ORG 0xe05b
post16:
        // enable cache
        movl %cr0, %eax
        andl $~(CR0_CD|CR0_NW), %eax
        movl %eax, %cr0

        // Check for restart indicator.
        movl $CMOS_RESET_CODE, %eax
        outb %al, $PORT_CMOS_INDEX
        inb $PORT_CMOS_DATA, %al
        cmpb $0x0, %al
        jnz entry_resume

        // Normal entry point
        RESET_STACK

        pushl $_code32__start

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
        inb $PORT_A20, %al
        orb $A20_ENABLE_BIT, %al
        outb %al, $PORT_A20

        // Set segment descriptors
        lidtw %cs:pmode_IDT_info
        lgdtw %cs:rombios32_gdt_48

        // Enable protected mode
        movl %cr0, %eax
        orl $CR0_PE, %eax
        movl %eax, %cr0

        // start 32bit protected mode code
        ljmpl $SEG32_MODE32_CS, $(BUILD_BIOS_ADDR + 1f)

        .code32
1:
        // init data segments
        movl $SEG32_MODE32_DS, %eax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        movw %ax, %fs
        movw %ax, %gs

        retl

// Call a 16bit function from 32bit mode.
// %eax = address of struct bregs
// Clobbers: all gp registers, flags, stack registers, cr0, idt/gdt
        .global __call16_from32, __call16big_from32
__call16_from32:
        pushl %eax

        // restore data segment limits to 0xffff
        movl $SEG32_MODE16_DS, %eax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        movw %ax, %fs
        movw %ax, %gs

        // disable a20
        inb $PORT_A20, %al
        andb $~A20_ENABLE_BIT, %al
        outb %al, $PORT_A20

        // Jump to 16bit mode
        ljmpw $SEG32_MODE16_CS, $1f

__call16big_from32:
        pushl %eax

        movl $SEG32_MODE16BIG_DS, %eax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        movw %ax, %fs
        movw %ax, %gs

        ljmpl $SEG32_MODE16BIG_CS, $(BUILD_BIOS_ADDR + 1f)

        .code16gcc
1:
        // Disable protected mode
        movl %cr0, %eax
        andl $~CR0_PE, %eax
        movl %eax, %cr0

        // far jump to flush CPU queue after transition to real mode
        ljmpw $SEG_BIOS, $2f

2:
        // restore IDT to normal real-mode defaults
        lidtw %cs:rmode_IDT_info

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
        .global __call16
__call16:
        // Save eax
        pushl %eax

        // Setup for iretw call
        pushw $SEG_BIOS
        pushw $1f               // return point
        pushw BREGS_flags(%eax) // flags
        pushl BREGS_ip(%eax)    // CS:IP

        // Load calling registers.
        movl BREGS_edi(%eax), %edi
        movl BREGS_esi(%eax), %esi
        movl BREGS_ebx(%eax), %ebx
        movl BREGS_edx(%eax), %edx
        movl BREGS_ecx(%eax), %ecx
        movw BREGS_es(%eax), %es
        movw BREGS_ds(%eax), %ds
        movl %ss:BREGS_eax(%eax), %eax

        // Invoke call
        iretw                   // XXX - just do a lcalll
1:
        // Store flags, eax, ecx
        pushfw
        pushl %eax
        movl 0x06(%esp), %eax
        movl %ecx, %ss:BREGS_ecx(%eax)
        movw %ds, %ss:BREGS_ds(%eax)
        movw %ss, %cx
        movw %cx, %ds           // Restore %ds == %ss
        popl %ecx
        movl %ecx, BREGS_eax(%eax)
        popw %cx
        movw %cx, BREGS_flags(%eax)

        // Store remaining registers
        movw %es, BREGS_es(%eax)
        movl %edi, BREGS_edi(%eax)
        movl %esi, BREGS_esi(%eax)
        movl %ebx, BREGS_ebx(%eax)
        movl %edx, BREGS_edx(%eax)

        // Remove %eax
        popl %eax

        cld

        retl

// Entry point when a post call looks like a resume.
// %eax = shutdown status from cmos
entry_resume:
        // Save old shutdown status.
        movl %eax, %ebx

        // Clear shutdown status register.
        movl $CMOS_RESET_CODE, %eax
        outb %al, $PORT_CMOS_INDEX
        xorl %eax, %eax
        outb %al, $PORT_CMOS_DATA

        // Use a stack in EBDA
        movw $SEG_BDA, %ax
        movw %ax, %ds
        movw BDA_ebda_seg, %ax
        movw %ax, %ss
        movw %ax, %ds
        movl $EBDA_OFFSET_TOP_STACK, %esp

        // Call handler.
        movl %ebx, %eax
        cld
        cli
        jmp handle_resume

// PnP trampolines
        .global entry_pnp_real, entry_pnp_prot
entry_pnp_prot:
        pushl %esp
        jmp 1f
entry_pnp_real:
        pushl %esp              // Backup %esp, then clear high bits
        movzwl %sp, %esp
1:
        pushfl                  // Save registers clobbered by C code
        pushl %eax
        pushl %ecx
        pushl %edx
        pushw %es
        pushw %ds
        movw %ss, %cx           // Move %ss to %ds
        movw %cx, %ds
        lea 28(%esp), %eax      // %eax points to start of u16 args
        calll handle_pnp
        movw %ax, 12(%esp)      // Modify %eax to return %ax
        popw %ds
        popw %es
        popl %edx
        popl %ecx
        popl %eax
        popfl
        popl %esp
        lretw

// APM trampolines
        .global apm16protected_entry
apm16protected_entry:
        pushfw          // save flags
        pushl %eax      // dummy
        ENTRY_ARG handle_1553
        addw $4, %sp    // pop dummy
        popfw           // restore flags
        lretw

        .code32
        .global apm32protected_entry
apm32protected_entry:
        pushfw
        pushw %cs       // Setup for long jump to 16bit mode
        pushw $1f
        addw $8, 2(%esp)
        ljmpw *(%esp)
        .code16gcc
1:
        ENTRY_ARG_ESP handle_1553

        movw $2f,(%esp) // Setup for long jump back to 32bit mode
        subw $8, 2(%esp)
        ljmpw *(%esp)
        .code32
2:
        addl $4, %esp   // pop call address
        popfw
        lretl

// 32bit elf entry point
        .global post32
post32:
        cli
        cld
        lidtl (BUILD_BIOS_ADDR + pmode_IDT_info)
        lgdtl (BUILD_BIOS_ADDR + rombios32_gdt_48)
        movl $BUILD_STACK_ADDR, %esp
        ljmpl $SEG32_MODE32_CS, $_code32__start

        .code16gcc

// Shutdown a CPU.  We want this in the 0xf000 section to ensure that
// the code wont be overwritten with something else.  (Should
// something spurious wake up the CPU, we want to be sure that the hlt
// insn will still be present and will shutdown the CPU.)
        .global permanent_halt
permanent_halt:
        cli
1:      hlt
        jmp 1b

        // IRQ trampolines
        .macro IRQ_TRAMPOLINE num
        .global irq_trampoline_0x\num
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


/****************************************************************
 * Interrupt entry points
 ****************************************************************/

        // Define an entry point for an interrupt (no args passed).
        .macro IRQ_ENTRY num
        .global entry_\num
        entry_\num :
        cli         // In case something far-calls instead of using "int"
        ENTRY handle_\num
        iretw
        .endm

        // Define an entry point for an interrupt (can read/modify args).
        .macro IRQ_ENTRY_ARG num
        .global entry_\num
        entry_\num :
        cli         // In case something far-calls instead of using "int"
        ENTRY_ARG handle_\num
        iretw
        .endm

        ORG 0xe2c3
        IRQ_ENTRY nmi

/****************************************************************
 * GDT and IDT tables (between 0xe2c3 - 0xe3fe)
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
        .type pmode_IDT_info, @object
pmode_IDT_info:
        .word 0x0000  // limit 15:00
        .long 0xf0000 // base 16:47

// Real mode IDT descriptor
//
// Set to typical real-mode values.
// base  = 000000
// limit =   03ff
        .type rmode_IDT_info, @object
rmode_IDT_info:
        .word 0x03ff  // limit 15:00
        .long 0       // base 16:47

        .type rombios32_gdt_48, @object
rombios32_gdt_48:
        .word (rombios32_gdt_end - rombios32_gdt)
        .long (BUILD_BIOS_ADDR + rombios32_gdt)

        .balign 8
        .type rombios32_gdt, @object
rombios32_gdt:
        .word 0, 0, 0, 0
        .word 0, 0, 0, 0
        // 32 bit flat code segment (SEG32_MODE32_CS)
        .word 0xffff, 0, 0x9b00, 0x00cf
        // 32 bit flat data segment (SEG32_MODE32_DS)
        .word 0xffff, 0, 0x9300, 0x00cf
        // 16 bit code segment base=0xf0000 limit=0xffff (SEG32_MODE16_CS)
        .word 0xffff, 0, 0x9b0f, 0x0000
        // 16 bit data segment base=0x0 limit=0xffff (SEG32_MODE16_DS)
        .word 0xffff, 0, 0x9300, 0x0000
        // 16 bit code segment base=0 limit=0xffffffff (SEG32_MODE16BIG_CS)
        .word 0xffff, 0, 0x9b00, 0x008f
        // 16 bit data segment base=0 limit=0xffffffff (SEG32_MODE16BIG_DS)
        .word 0xffff, 0, 0x9300, 0x008f
rombios32_gdt_end:


/****************************************************************
 * Interrupt entry points (continued)
 ****************************************************************/

        ORG 0xe3fe
        .global entry_13_official
entry_13_official:
        jmp entry_13

        ORG 0xe401
        .type __fdpt, @object
__fdpt:
        // XXX - Fixed Disk Parameter Table

        ORG 0xe6f2
        .global entry_19_official
entry_19_official:
        jmp entry_19

        ORG 0xe6f5
.include "out/cbt.proc.16.s"
        .text

        ORG 0xe729
        .type __brgt, @object
__brgt:
        // XXX - Baud Rate Generator Table

        ORG 0xe739
        IRQ_ENTRY_ARG 14

        ORG 0xe82e
        IRQ_ENTRY_ARG 16

        ORG 0xe987
        IRQ_ENTRY 09

        ORG 0xec59
        IRQ_ENTRY_ARG 40

        ORG 0xef57
        IRQ_ENTRY 0e

        ORG 0xefc7
.include "out/floppy_dbt.proc.16.s"
        .text

        ORG 0xefd2
        IRQ_ENTRY_ARG 17

        ORG 0xf045
__int10_0x0f:
        // XXX - INT 10 Functions 0-Fh Entry Point
        iretw

        ORG 0xf065
        IRQ_ENTRY_ARG 10

        ORG 0xf0a4
        .type __int1d, @object
__int1d:
        // XXX - INT 1D - SYSTEM DATA - VIDEO PARAMETER TABLES
        .space 0x58

        .global freespace2_start, freespace2_end
freespace2_start:

        ORG 0xf841
freespace2_end:
        .global entry_12_official
entry_12_official:
        jmp entry_12

        ORG 0xf84d
        .global entry_11_official
entry_11_official:
        jmp entry_11

        ORG 0xf859
        IRQ_ENTRY_ARG 15

        // Fit other misc defs if the freespace between 0xf859-0xfa6e

        IRQ_ENTRY_ARG 13
        IRQ_ENTRY_ARG 12
        IRQ_ENTRY_ARG 11
        IRQ_ENTRY 76
        IRQ_ENTRY 1c
        IRQ_ENTRY 70
        IRQ_ENTRY 74
        IRQ_ENTRY 75
        IRQ_ENTRY hwpic1
        IRQ_ENTRY hwpic2

        // int 18/19 are special - they reset the stack and do not return.
entry_19:
        RESET_STACK
        pushl $_code32_handle_19
        jmp transition32

        .global entry_18
entry_18:
        RESET_STACK
        pushl $_code32_handle_18
        jmp transition32

        ORG 0xfa6e
.include "out/font.proc.16.s"
        .text

        ORG 0xfe6e
        IRQ_ENTRY_ARG 1a

        ORG 0xfea5
        IRQ_ENTRY 08

        ORG 0xfef3
__initvector:
        // XXX - Initial Interrupt Vector Offsets Loaded by POST

        ORG 0xff00
        .type __copyright, @object
__copyright:
        // XXX - BIOS_COPYRIGHT_STRING
        .asciz "(c) 2002 MandrakeSoft S.A. Written by Kevin Lawton & the Bochs team."

        ORG 0xff53
        .global dummy_iret_handler
dummy_iret_handler:
        iretw

        ORG 0xff54
        IRQ_ENTRY_ARG 05

        ORG 0xfff0 // Power-up Entry Point
        .global reset_vector
reset_vector:
        ljmpw $SEG_BIOS, $post16

        ORG 0xfff5
        .type __biosdate, @object
__biosdate:
        // BIOS build date
        .ascii "06/23/99"

        ORG 0xfffe
        .type __model_id, @object
__model_id:
        .byte CONFIG_MODEL_ID

        .global bios_checksum
        .type bios_checksum, @object
bios_checksum:
        .byte 0x00

        .end
