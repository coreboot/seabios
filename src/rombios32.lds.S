// Linker definitions for 32 bit code
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "config.h"

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH("i386")
SECTIONS
{
        . = BUILD_BIOS_ADDR ;
        code32_start = . ;
        .text : {
                *(.text)
                code32_rodata = . ;
                *(.rodata*)
                *(.data)
                . = ALIGN(16) ;
                code32_bss = . ;
                *(.bss)
                *(COMMON)
                }
        code32_end = . ;
}
