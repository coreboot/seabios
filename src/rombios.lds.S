// Linker definitions for merging 16 and 32 bit code
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "config.h" // BUILD_BIOS_ADDR

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH("i386")
ENTRY(post32)
SECTIONS
{
        .text code32flat_start : {
                *(.text32flat)

                . = code32seg_start + BUILD_BIOS_ADDR - code32flat_start ;
                *(.text32seg)

                . = data16_start + BUILD_BIOS_ADDR - code32flat_start ;
                *(.data16)
                . = text16_start + BUILD_BIOS_ADDR - code32flat_start ;
                *(.text16)
                final_text16_end = . ;
                }
        /DISCARD/ : {
                *(.text*) *(.data*) *(.bss*) *(.rodata*)
                *(COMMON) *(.discard*) *(.eh_frame)
                }
}
