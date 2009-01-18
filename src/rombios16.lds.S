// Linker definitions for 16bit code
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "config.h" // BUILD_BIOS_ADDR

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH("i386")
SECTIONS
{
        . = ( _code32_code32_end - BUILD_BIOS_ADDR ) ;
        . = ALIGN(16) ;
        code16_start = . ;
        .text16 : {

// The actual placement of the 16bit sections is determined by the
// script tools/layoutrom.py
#include "../out/romlayout.lds"

                }
        code16_end = . ;

        // Discard regular data sections to force a link error if
        // 16bit code attempts to access data not marked with VAR16.
        /DISCARD/ : { *(.text*) *(.rodata*) *(.data*) *(.bss*) *(COMMON) }
}
