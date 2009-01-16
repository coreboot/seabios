// Linker definitions for 16bit code
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "config.h"

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH("i386")
SECTIONS
{
        . = ( _code32_code32_end - BUILD_BIOS_ADDR ) ;
        code16_start = . ;
        .text16 : {
                *(.text)
                code16_rodata = . ;
                *(.rodata*)
                *(.data.var16)
                }
        freespace1_start = . ;
        code16_end = . ;

        . = BUILD_START_FIXED ;
        code16_fixed_start = . ;
        freespace1_end = . ;
        .text16.fixed.addr : {
                *(.text.fixed.addr)
                }
        code16_fixed_end = . ;

        // Discard regular data sections to force a link error if
        // 16bit code attempts to access data not marked with VAR16.
        /DISCARD/ : { *(.data) *(.bss) *(COMMON) }
}
