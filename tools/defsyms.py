#!/usr/bin/env python
# Simple script to convert the output from 'nm' to a C style header
# file with defined offsets.
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

import sys
import string

def main():
    syms = []
    lines = sys.stdin.readlines()
    for line in lines:
        addr, type, sym = line.split()
        if type not in 'TA':
            # Only interested in global symbols in text segment
            continue
        for c in sym:
            if c not in string.letters + string.digits + '_':
                break
        else:
            syms.append((sym, addr))
    print """
#ifndef __OFFSET16_AUTO_H
#define __OFFSET16_AUTO_H
// Auto generated file - please see defsyms.py.
// This file contains symbol offsets of a compiled binary.
"""
    for sym, addr in syms:
        print "#define OFFSET_%s 0x%s" % (sym, addr)
    print """
#endif // __OFFSET16_AUTO_H
"""

if __name__ == '__main__':
    main()
