// Macros for entering C code
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.


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
