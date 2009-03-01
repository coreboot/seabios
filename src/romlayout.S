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
        cli         // In case something far-calls instead of using "int"
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
        cli
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
        cli
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

        // Reset stack, transition to 32bit mode, and call a C function.
        // Clobbers %ax
        .macro ENTRY_INTO32 cfunc
        xorw %ax, %ax
        movw %ax, %ss
        movl $ BUILD_STACK_ADDR , %esp
        pushl $ \cfunc
        jmp transition32
        .endm

        // Declare a function
        .macro DECLFUNC func
        .section .text.asm.\func
        .global \func
        .endm


/****************************************************************
 * POST handler
 ****************************************************************/

        DECLFUNC entry_post
entry_post:
        // Enable cache
        movl %cr0, %eax
        andl $~(CR0_CD|CR0_NW), %eax
        movl %eax, %cr0

        // Disable interrupts
        cli
        cld

        // Check for restart indicator.
        movl $CMOS_RESET_CODE, %eax
        outb %al, $PORT_CMOS_INDEX
        inb $PORT_CMOS_DATA, %al
        cmpb $0x0, %al
        jnz 1f

        // Normal entry point
        ENTRY_INTO32 _code32__start

        // Entry point when a post call looks like a resume.
1:
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
        // XXX - should verify ebda_seg looks sane.
        movw %ax, %ds
        movw %ax, %ss
        movl $EBDA_OFFSET_TOP_STACK, %esp

        // Call handler.
        movl %ebx, %eax
        jmp handle_resume


/****************************************************************
 * Call trampolines
 ****************************************************************/

// Place CPU into 32bit mode from 16bit mode.
// Clobbers: flags, segment registers, cr0, idt/gdt
// Require: interrupts must be disabled
        DECLFUNC transition32
transition32:
        pushl %eax

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

        popl %eax
        retl

// Call a 16bit function from 32bit mode.
// %eax = address of struct bregs
// Clobbers: %e[bcd]x, %e[ds]i, flags, segment registers, idt/gdt
        DECLFUNC __call16_from32
        .global __call16big_from32
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

        // Make call.
        calll __call16

        // Return via transition32
        jmp transition32


// Call a 16bit function from 16bit mode with a specified cpu register state
// %eax = address of struct bregs
// Clobbers: %e[bcd]x, %e[ds]i, flags
        DECLFUNC __call16
__call16:
        // Save %eax, %ebp
        pushl %ebp
        pushl %eax

        // Setup for iretw call
        pushw %cs
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

        // Remove %eax, restore %ebp
        popl %eax
        popl %ebp

        cli
        cld

        retl

// PnP trampolines
        DECLFUNC entry_pnp_real
        .global entry_pnp_prot
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
        DECLFUNC apm16protected_entry
apm16protected_entry:
        pushfw          // save flags
        pushl %eax      // dummy
        ENTRY_ARG handle_1553
        addw $4, %sp    // pop dummy
        popfw           // restore flags
        lretw

        .code32
        DECLFUNC apm32protected_entry
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
        DECLFUNC post32
post32:
        cli
        cld
        lidtl (BUILD_BIOS_ADDR + pmode_IDT_info)
        lgdtl (BUILD_BIOS_ADDR + rombios32_gdt_48)
        movl $BUILD_STACK_ADDR, %esp
        ljmpl $SEG32_MODE32_CS, $_code32__start

        .code16gcc

// IRQ trampolines
        .macro IRQ_TRAMPOLINE num
        DECLFUNC irq_trampoline_0x\num
        irq_trampoline_0x\num :
        int $0x\num
        lretw
        .endm

        IRQ_TRAMPOLINE 10
        IRQ_TRAMPOLINE 13
        IRQ_TRAMPOLINE 15
        IRQ_TRAMPOLINE 16
        IRQ_TRAMPOLINE 18
        IRQ_TRAMPOLINE 19


/****************************************************************
 * Interrupt entry points
 ****************************************************************/

        // Define an entry point for an interrupt (no args passed).
        .macro IRQ_ENTRY num
        .global entry_\num
        entry_\num :
        ENTRY handle_\num
        iretw
        .endm

        // Define an entry point for an interrupt (can read/modify args).
        .macro IRQ_ENTRY_ARG num
        .global entry_\num
        entry_\num :
        ENTRY_ARG handle_\num
        iretw
        .endm

        // Macros that put each handler into its own section
        .macro DECL_IRQ_ENTRY num
        .section .text.asm.entry_\num
        IRQ_ENTRY \num
        .endm
        .macro DECL_IRQ_ENTRY_ARG num
        .section .text.asm.entry_\num
        IRQ_ENTRY_ARG \num
        .endm

        DECL_IRQ_ENTRY_ARG 13
        DECL_IRQ_ENTRY_ARG 12
        DECL_IRQ_ENTRY_ARG 11
        DECL_IRQ_ENTRY 76
        DECL_IRQ_ENTRY 70
        DECL_IRQ_ENTRY 74
        DECL_IRQ_ENTRY 75
        DECL_IRQ_ENTRY hwpic1
        DECL_IRQ_ENTRY hwpic2

        // int 18/19 are special - they reset stack and call into 32bit mode.
        DECLFUNC entry_19
entry_19:
        cli
        cld
        ENTRY_INTO32 _code32_handle_19

        DECLFUNC entry_18
entry_18:
        cli
        cld
        ENTRY_INTO32 _code32_handle_18


/****************************************************************
 * Fixed position entry points
 ****************************************************************/

        // Specify a location in the fixed part of bios area.
        .macro ORG addr
        .section .fixedaddr.\addr
        .endm

        ORG 0xe05b
entry_post_official:
        jmp entry_post

        ORG 0xe2c3
        IRQ_ENTRY nmi

        ORG 0xe3fe
        .global entry_13_official
entry_13_official:
        jmp entry_13

        // 0xe401 - OldFDPT in disk.c

        ORG 0xe6f2
        .global entry_19_official
entry_19_official:
        jmp entry_19

        // 0xe6f5 - BIOS_CONFIG_TABLE in misc.c

        // 0xe729 - BaudTable in serial.c

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

        // 0xefc7 - diskette_param_table in floppy.c

        ORG 0xefd2
        IRQ_ENTRY_ARG 17

        ORG 0xf045
entry_10_0x0f:
        // XXX - INT 10 Functions 0-Fh Entry Point
        iretw

        ORG 0xf065
        IRQ_ENTRY_ARG 10

        // 0xf0a4 - VideoParams in misc.c

        ORG 0xf841
        .global entry_12_official
entry_12_official:
        jmp entry_12

        ORG 0xf84d
        .global entry_11_official
entry_11_official:
        jmp entry_11

        ORG 0xf859
        IRQ_ENTRY_ARG 15

        // 0xfa6e - vgafont8 in font.c

        ORG 0xfe6e
        IRQ_ENTRY_ARG 1a

        ORG 0xfea5
        IRQ_ENTRY 08

        // 0xfef3 - InitVectors in misc.c

        // 0xff00 - BiosCopyright in misc.c

        ORG 0xff53
        .global entry_iret_official
entry_iret_official:
        iretw

        ORG 0xff54
        IRQ_ENTRY_ARG 05

        ORG 0xfff0 // Power-up Entry Point
        .global reset_vector
reset_vector:
        ljmpw $SEG_BIOS, $entry_post_official

        // 0xfff5 - BiosDate in misc.c

        // 0xfffe - BiosModelId in misc.c

        // 0xffff - BiosChecksum in misc.c

        .end
