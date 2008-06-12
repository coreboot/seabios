// Linker definitions for 16bit code
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "config.h"

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH("i386")
ENTRY(post16);
SECTIONS
{
        . = 0;
        .text     : {
                *(.text)
                *(.rodata)
                *(.rodata.*)
                *(.data)
                bios16c_end = .;
                . = BUILD_START_FIXED;
                *(.text.fixed.addr)
                }

        /DISCARD/ : { *(.stab)
                     *(.stabstr)
                     *(.comment)
                     *(.note)
                  }
}
