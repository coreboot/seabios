// Rom layout and bios assembler to C interface.
//
// Copyright (C) 2009-2012  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.


#include "config.h" // CONFIG_*
#include "entryfuncs.S" // ENTRY_*


/****************************************************************
 * Rom Header
 ****************************************************************/

        .section .rom.header
        .code16gcc
        .global _rom_header, _rom_header_size, _rom_header_checksum
_rom_header:
        .word 0xaa55
_rom_header_size:
        .byte 0
_rom_header_entry:
        jmp _optionrom_entry
_rom_header_checksum:
        .byte 0
_rom_header_other:
        .space 17
_rom_header_pcidata:
#if CONFIG_VGA_PCI == 1
        .word rom_pci_data
#else
        .word 0
#endif
_rom_header_pnpdata:
        .word 0
_rom_header_other2:
        .word 0
_rom_header_signature:
        .asciz "IBM"


/****************************************************************
 * Entry points
 ****************************************************************/

        // This macro is the same as ENTRY_ARG except the "calll"
        // instruction is avoided to work around known issues in the
        // emulation of some versions of x86emu.
        .macro ENTRY_ARG_VGA cfunc
        cli
        cld
        PUSHBREGS
        movw %ss, %ax           // Move %ss to %ds
        movw %ax, %ds
        movl %esp, %ebx         // Backup %esp, then zero high bits
        movzwl %sp, %esp
        movl %esp, %eax         // First arg is pointer to struct bregs
        pushw %ax ; callw \cfunc
        movl %ebx, %esp         // Restore %esp (including high bits)
        POPBREGS
        .endm

        DECLFUNC entry_104f05
entry_104f05:
        ENTRY_ARG_VGA vbe_104f05
        lretw

        DECLFUNC _optionrom_entry
_optionrom_entry:
        ENTRY_ARG_VGA vga_post
        lretw

        DECLFUNC entry_10
entry_10:
        ENTRY_ARG_VGA handle_10
        iretw
