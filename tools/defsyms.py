#!/usr/bin/env python
# Simple script to convert the output from 'nm' to a C style header
# file with defined offsets.
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

import sys
import string

def printUsage():
    print "Usage:\n   %s <output file>" % (sys.argv[0],)
    sys.exit(1)

def main():
    if len(sys.argv) != 2:
        printUsage()
    # Find symbols (that are valid)
    syms = []
    lines = sys.stdin.readlines()
    for line in lines:
        addr, type, sym = line.split()
        if type not in 'Tt':
            # Only interested in global symbols in text segment
            continue
        for c in sym:
            if c not in string.letters + string.digits + '_':
                break
        else:
            syms.append((sym, addr))
    # Build guard string
    guardstr = ''
    for c in sys.argv[1]:
        if c not in string.letters + string.digits + '_':
            guardstr += '_'
        else:
            guardstr += c
    # Generate header
    f = open(sys.argv[1], 'wb')
    f.write("""
#ifndef __OFFSET_AUTO_H__%s
#define __OFFSET_AUTO_H__%s
// Auto generated file - please see defsyms.py.
// This file contains symbol offsets of a compiled binary.

""" % (guardstr, guardstr))
    for sym, addr in syms:
        f.write("#define OFFSET_%s 0x%s\n" % (sym, addr))
    f.write("""
#endif // __OFFSET_AUTO_H__%s
""" % (guardstr,))

if __name__ == '__main__':
    main()
