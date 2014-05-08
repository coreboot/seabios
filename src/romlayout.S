// Rom layout and bios assembler to C interface.
//
// Copyright (C) 2008-2012  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "asm-offsets.h" // BREGS_*
#include "config.h" // CONFIG_*
#include "entryfuncs.S" // ENTRY_*
#include "hw/ps2port.h" // PORT_A20
#include "hw/rtc.h" // CMOS_RESET_CODE
#include "x86.h" // CR0_*


/****************************************************************
 * 16bit / 32bit call trampolines
 ****************************************************************/

// Place CPU into 32bit mode from 16bit mode.
// %edx = return location (in 32bit mode)
// Clobbers: ecx, flags, segment registers, cr0, idt/gdt
        DECLFUNC transition32
        .code16gcc
transition32:
        movl %eax, %ecx

        // Disable irqs (and clear direction flag)
        cli
        cld

        // Disable nmi
        movl $CMOS_RESET_CODE|NMI_DISABLE_BIT, %eax
        outb %al, $PORT_CMOS_INDEX
        inb $PORT_CMOS_DATA, %al

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

        movl %ecx, %eax
        jmpl *%edx

// Place CPU into 16bit mode from 32bit mode.
// %edx = return location (in 16bit mode)
// Clobbers: ecx, flags, segment registers, cr0, idt/gdt
        DECLFUNC transition16
        .global transition16big
transition16:
        movl %eax, %ecx

        // restore data segment limits to 0xffff
        movl $SEG32_MODE16_DS, %eax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        movw %ax, %fs
        movw %ax, %gs

#if CONFIG_DISABLE_A20
        // disable a20
        inb $PORT_A20, %al
        andb $~A20_ENABLE_BIT, %al
        outb %al, $PORT_A20
#endif

        // Jump to 16bit mode
        ljmpw $SEG32_MODE16_CS, $1f

transition16big:
        movl %eax, %ecx

        movl $SEG32_MODE16BIG_DS, %eax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        movw %ax, %fs
        movw %ax, %gs

        ljmpw $SEG32_MODE16BIG_CS, $1f

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

        movl %ecx, %eax
        jmpl *%edx

// Call a 16bit SeaBIOS function from SeaBIOS 32bit C code.
// %ecx = calling function
// Clobbers: %ecx, %edx, flags, segment registers, idt/gdt
        DECLFUNC __call16
        .global __call16big
        .code32
__call16:
        pushl %edx
        pushl %ecx
        movl $1f, %edx
        jmp transition16
__call16big:
        pushl %edx
        pushl %ecx
        movl $1f, %edx
        jmp transition16big

        // Make call.
        .code16gcc
1:      movl $_zonelow_seg, %edx        // Adjust %ds, %ss, and %esp
        movl %edx, %ds
        movzwl StackSeg, %edx
        movl %edx, %ecx
        shll $4, %ecx
        movl %edx, %ss
        subl %ecx, %esp
        movl %edx, %ds

        popl %ecx                       // Call function
        popl %edx
        calll *%ecx

        movl %ss, %edx                  // Readjust %esp
        shll $4, %edx
        addl %edx, %esp

        // Return via transition32
        movl $(2f + BUILD_BIOS_ADDR), %edx
        jmp transition32
        .code32
2:      retl


/****************************************************************
 * External calling trampolines
 ****************************************************************/

// Far call a 16bit function from 16bit mode with a specified cpu register state
// %eax = address of struct bregs, %edx = segment of struct bregs
// Clobbers: %e[bc]x, %e[ds]i, flags
        .code16gcc
        DECLFUNC __farcall16
__farcall16:
        // Save %edx/%eax, %ebp
        pushl %ebp
        pushl %eax
        pushl %edx

        // Setup for iretw call
        movl %edx, %ds
        pushw %cs
        pushw $1f                       // return point
        pushw BREGS_flags(%eax)         // flags
        pushl BREGS_code(%eax)          // CS:IP

        // Load calling registers.
        movl BREGS_edi(%eax), %edi
        movl BREGS_esi(%eax), %esi
        movl BREGS_ebp(%eax), %ebp
        movl BREGS_ebx(%eax), %ebx
        movl BREGS_edx(%eax), %edx
        movl BREGS_ecx(%eax), %ecx
        movw BREGS_es(%eax), %es
        pushl BREGS_eax(%eax)
        movw BREGS_ds(%eax), %ds
        popl %eax

        // Invoke call
        iretw                           // XXX - just do a lcalll
1:
        // Store flags, es, eax
        pushfw
        cli
        cld
        pushw %ds
        pushl %eax
        movw 0x08(%esp), %ds
        movl 0x0c(%esp), %eax
        popl BREGS_eax(%eax)
        popw BREGS_es(%eax)
        popw BREGS_flags(%eax)

        // Store remaining registers
        movl %edi, BREGS_edi(%eax)
        movl %esi, BREGS_esi(%eax)
        movl %ebp, BREGS_ebp(%eax)
        movl %ebx, BREGS_ebx(%eax)
        movl %edx, BREGS_edx(%eax)
        movl %ecx, BREGS_ecx(%eax)
        movw %es, BREGS_ds(%eax)
        movw %ss, %cx
        movw %cx, %ds                   // Restore %ds == %ss

        // Remove %edx/%eax, restore %ebp
        popl %edx
        popl %eax
        popl %ebp

        retl

// IRQ trampolines
        .macro IRQ_TRAMPOLINE num
        DECLFUNC irq_trampoline_0x\num
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
 * Misc. entry points.
 ****************************************************************/

// Resume (and reboot) entry point - called from entry_post
        DECLFUNC entry_resume
entry_resume:
        // Disable interrupts
        cli
        cld
        // Use the ExtraStack in low mem.
        movl $_zonelow_seg, %eax
        movw %ax, %ds
        movw %ax, %ss
        movl $ExtraStack + BUILD_EXTRA_STACK_SIZE, %esp
        // Call handler.
        jmp handle_resume

// PMM entry point
        DECLFUNC entry_pmm
entry_pmm:
        pushl %esp              // Backup %esp, then clear high bits
        movzwl %sp, %esp
        pushfl                  // Save registers clobbered by C code
        cli
        cld
        pushl %eax
        pushl %ecx
        pushl %edx
        pushw %es
        pushw %ds
        movw %ss, %cx           // Move %ss to %ds
        movw %cx, %ds
        movl $_cfunc32flat_handle_pmm, %eax // Setup: call32(handle_pmm, args, -1)
        leal 28(%esp), %edx     // %edx points to start of args
        movl $-1, %ecx
        calll call32
        movw %ax, 12(%esp)      // Modify %ax:%dx to return %eax
        shrl $16, %eax
        movw %ax, 4(%esp)
        popw %ds                // Restore saved registers
        popw %es
        popl %edx
        popl %ecx
        popl %eax
        popfl
        popl %esp
        lretw

// PnP entry points
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
        cli
        cld
        pushl %eax
        pushl %ecx
        pushl %edx
        pushw %es
        pushw %ds
        movw %ss, %cx           // Move %ss to %ds
        movw %cx, %ds
        leal 28(%esp), %eax     // %eax points to start of u16 args
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

// APM entry points
        DECLFUNC entry_apm16
entry_apm16:
        pushfw          // save flags
        pushl %eax      // dummy
        ENTRY_ARG handle_apm
        addw $4, %sp    // pop dummy
        popfw           // restore flags
        lretw

        .code32
        DECLFUNC entry_apm32
entry_apm32:
        pushfl
        pushl %gs
        pushl %cs               // Move second descriptor after %cs to %gs
        addl $16, (%esp)
        popl %gs
        ENTRY_ARG_ESP _cfunc32seg_handle_apm
        popl %gs
        popfl
        lretl

// PCI-BIOS entry points
        DECLFUNC entry_pcibios32
entry_pcibios32:
        pushfl
        pushl %gs               // Backup %gs and set %gs=%ds
        pushl %ds
        popl %gs
        ENTRY_ARG_ESP _cfunc32seg_handle_pcibios
        popl %gs
        popfl
        lretl

        .code16gcc
        DECLFUNC entry_pcibios16
entry_pcibios16:
        ENTRY_ARG handle_pcibios
        iretw

// int 1589 entry point
        DECLFUNC entry_1589
entry_1589:
        ENTRY_ARG handle_1589
        iretw

// BIOS32 support
        .code32
        DECLFUNC entry_bios32
entry_bios32:
        pushfl
#if CONFIG_PCIBIOS
        // Check for PCI-BIOS request
        cmpl $0x49435024, %eax // $PCI
        jne 1f
        movl $BUILD_BIOS_ADDR, %ebx
        movl $BUILD_BIOS_SIZE, %ecx
        movl $entry_pcibios32, %edx
        xorb %al, %al
        jmp 2f
#endif
        // Unknown request
1:      movb $0x80, %al
        // Return to caller
2:      popfl
        lretl

// 32bit elf entry point
        EXPORTFUNC entry_elf
entry_elf:
        cli
        cld
        lidtl (BUILD_BIOS_ADDR + pmode_IDT_info)
        lgdtl (BUILD_BIOS_ADDR + rombios32_gdt_48)
        movl $SEG32_MODE32_DS, %eax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %fs
        movw %ax, %gs
        movw %ax, %ss
        movl $BUILD_STACK_ADDR, %esp
        ljmpl $SEG32_MODE32_CS, $_cfunc32flat_handle_post

        .code16gcc

// UEFI Compatibility Support Module (CSM) entry point
        EXPORTFUNC entry_csm
entry_csm:
        // Backup register state
        pushfw
        cli
        cld
        pushl %eax                      // dummy
        PUSHBREGS

        // Backup stack location and convert to a "flat pointer"
        movl %ss, %eax
        movw %ax, BREGS_code+2(%esp)    // Store %ss in bregs->code.seg
        shll $4, %eax
        addl %esp, %eax

        // Change to BUILD_STACK_ADDR stack
        xorl %ebx, %ebx
        movw %bx, %ss
        movl $BUILD_STACK_ADDR, %esp

        // Jump to 32bit mode and call handle_csm(bregs)
        movl $_cfunc32flat_handle_csm, %edx
        jmp transition32

        DECLFUNC __csm_return
        .code32
__csm_return:
        movl $1f, %edx
        jmp transition16big
        .code16gcc

        // Switch back to original stack
1:      movzwl BREGS_code+2(%eax), %edx
        movl %edx, %ecx
        shll $4, %ecx
        subl %ecx, %eax
        movl %edx, %ss
        movl %eax, %esp

        // Restore register state and return.
        POPBREGS
        addw $4, %sp                    // pop dummy
        popfw
        lretw


/****************************************************************
 * Interrupt entry points
 ****************************************************************/

        // Main entry point for interrupts handled on extra stack
        DECLFUNC irqentry_extrastack
irqentry_extrastack:
        cli
        cld
        pushw %ds               // Set %ds:%eax to space on ExtraStack
        pushl %eax
        movl $_zonelow_seg, %eax
        movl %eax, %ds
        movl StackPos, %eax
        subl $24, %eax
        popl 0(%eax)            // Backup %eax, %ds, %es, %ecx, %edx
        popw 4(%eax)
        movw %es, 6(%eax)
        movl %ecx, 8(%eax)
        popl %ecx
        movl %edx, 12(%eax)
        movl %esp, 16(%eax)
        movw %ss, 20(%eax)

        movw %ds, %dx           // Setup %ss/%esp and call function
        movw %dx, %ss
        movl %eax, %esp
        calll *%ecx

        movl %esp, %eax         // Restore registers and return
        movw 20(%eax), %ss
        movl 16(%eax), %esp
        movl 12(%eax), %edx
        movl 8(%eax), %ecx
        movw 6(%eax), %es
        pushw 4(%eax)
        pushl 0(%eax)
        popl %eax
        popw %ds
        iretw

        // Main entry point for interrupts handled on extra stack
        DECLFUNC irqentry_arg_extrastack
irqentry_arg_extrastack:
        cli
        cld
        pushw %ds               // Set %ds:%eax to space on ExtraStack
        pushl %eax
        movl $_zonelow_seg, %eax
        movl %eax, %ds
        movl StackPos, %eax
        subl $BREGS_size+8, %eax
        popl BREGS_eax(%eax)    // Backup registers
        popw BREGS_ds(%eax)
        movl %edi, BREGS_edi(%eax)
        movl %esi, BREGS_esi(%eax)
        movl %ebp, BREGS_ebp(%eax)
        movl %ebx, BREGS_ebx(%eax)
        movl %edx, BREGS_edx(%eax)
        movl %ecx, BREGS_ecx(%eax)
        popl %ecx
        movw %es, BREGS_es(%eax)
        movl %esp, BREGS_size+0(%eax)
        movw %ss, BREGS_size+4(%eax)
        popl BREGS_code(%eax)
        popw BREGS_flags(%eax)

        movw %ds, %dx           // Setup %ss/%esp and call function
        movw %dx, %ss
        movl %eax, %esp
        calll *%ecx

        movl %esp, %eax         // Restore registers and return
        movw BREGS_size+4(%eax), %ss
        movl BREGS_size+0(%eax), %esp
        popl %edx
        popw %dx
        pushw BREGS_flags(%eax)
        pushl BREGS_code(%eax)
        movl BREGS_edi(%eax), %edi
        movl BREGS_esi(%eax), %esi
        movl BREGS_ebp(%eax), %ebp
        movl BREGS_ebx(%eax), %ebx
        movl BREGS_edx(%eax), %edx
        movl BREGS_ecx(%eax), %ecx
        movw BREGS_es(%eax), %es
        pushw BREGS_ds(%eax)
        pushl BREGS_eax(%eax)
        popl %eax
        popw %ds
        iretw

        // Main entry point for interrupts with args
        DECLFUNC irqentryarg
irqentryarg:
        ENTRY_ARG_ST
        iretw

        // Define an entry point for hardware interrupts.
        .macro IRQ_ENTRY num
        .global entry_\num
        entry_\num :
        pushl $ handle_\num
        jmp irqentry_extrastack
        .endm

        .macro DECL_IRQ_ENTRY num
        DECLFUNC entry_\num
        IRQ_ENTRY \num
        .endm

        // Define an entry point for an interrupt (can read/modify args).
        .macro IRQ_ENTRY_ARG num
        .global entry_\num
        entry_\num :
        pushl $ handle_\num
#if CONFIG_ENTRY_EXTRASTACK
        jmp irqentry_arg_extrastack
#else
        jmp irqentryarg
#endif
        .endm

        .macro DECL_IRQ_ENTRY_ARG num
        DECLFUNC entry_\num
        IRQ_ENTRY_ARG \num
        .endm

        // Various entry points (that don't require a fixed location).
        DECL_IRQ_ENTRY_ARG 13
        DECL_IRQ_ENTRY 76
        DECL_IRQ_ENTRY 70
        DECL_IRQ_ENTRY 74
        DECL_IRQ_ENTRY 75
        DECL_IRQ_ENTRY hwpic1
        DECL_IRQ_ENTRY hwpic2

        // int 18/19 are special - they reset stack and call into 32bit mode.
        DECLFUNC entry_19
entry_19:
        ENTRY_INTO32 _cfunc32flat_handle_19

        DECLFUNC entry_18
entry_18:
        ENTRY_INTO32 _cfunc32flat_handle_18


/****************************************************************
 * Fixed position entry points
 ****************************************************************/

        // Specify a location in the fixed part of bios area.
        .macro ORG addr
        .section .fixedaddr.\addr
        .endm

        ORG 0xe05b
entry_post:
        cmpl $0, %cs:HaveRunPost                // Check for resume/reboot
        jnz entry_resume
        ENTRY_INTO32 _cfunc32flat_handle_post   // Normal entry point

        ORG 0xe2c3
        IRQ_ENTRY 02

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
        IRQ_ENTRY_ARG 12

        ORG 0xf84d
        IRQ_ENTRY_ARG 11

        ORG 0xf859
        .global entry_15_official
entry_15_official:
        cmpb $0x89, %ah
        je entry_1589           // 1589 calls return in protected mode
        IRQ_ENTRY_ARG 15

        // 0xfa6e - vgafont8 in font.c

        ORG 0xfe6e
        .global entry_1a_official
entry_1a_official:
        cmpb $0xb1, %ah
        je entry_pcibios16      // PCIBIOS calls can be in protected mode
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
        ljmpw $SEG_BIOS, $entry_post

        // 0xfff5 - BiosDate in misc.c

        // 0xfffe - BiosModelId in misc.c

        // 0xffff - BiosChecksum in misc.c

        .end
