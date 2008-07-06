// Linker definitions for 16 and 32 bit code
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "config.h"

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH("i386")
ENTRY(post32);
SECTIONS
{
        .text BUILD_BIOS_ADDR : {
                code32_start = . ;
                *(.text)
                *(.rodata*)
                *(.data)
                code32_end = . ;

                . = ALIGN(16) ;
                code16_start = . ;
                *(.text16)
                code16_end = . ;
                freespace1_start = . ;

                . = BUILD_START_FIXED ;
                freespace1_end = . ;
                *(.text16.fixed.addr)
                }

        .bss BUILD_BSS_ADDR : {
                 __bss_start = . ;
                 *(.bss)
                 *(COMMON)
                 __bss_end = . ;
                 }
}
