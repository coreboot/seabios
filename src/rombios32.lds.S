// Linker definitions for 32bit code
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "config.h"
#include "../out/rom16.offset.auto.h"

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH("i386")
ENTRY(_start);
SECTIONS
{
        . = (OFFSET_bios16c_end | 0xf0000);
        . = ALIGN(16);
        _text32_start = . ;
        .text     : { *(.text)    }
        .rodata    : { *(.rodata) }
        .data     : { *(.data)    }
        // XXX - should change code so it doesn't require global variables.
        . = 0x00040000;
        __bss_start = . ;
        .bss      : { *(.bss) *(COMMON) }
        __bss_end = . ;
        __call16_from32 = (0xf0000 | OFFSET___call16_from32) ;
        /DISCARD/ : { *(.stab)
                     *(.stabstr)
                     *(.comment)
                     *(.note)
                  }
}
