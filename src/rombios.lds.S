// Linker definitions for merging 16 and 32 bit code
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH("i386")
ENTRY(post32)
SECTIONS
{
        .text code32_start : {
                *(.text32)
                *(.data32.version)

                . = code16_start ;
                *(.text16)
                final_code16_end = . ;
                }
        /DISCARD/ : {
                *(.text*) *(.data*) *(.bss*) *(.rodata*)
                *(COMMON) *(.discard*) *(.eh_frame)
                }
}
