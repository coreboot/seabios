#!/usr/bin/env python
# Script to arrange sections to ensure fixed offsets.
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

import sys

def main():
    # Read in section names and sizes

    # sections = [(idx, name, size, align), ...]
    sections = []
    for line in sys.stdin.readlines():
        try:
            idx, name, size, vma, lma, fileoff, align = line.split()
            if align[:3] != '2**':
                continue
            sections.append((
                int(idx), name, int(size, 16), int(align[3:])))
        except:
            pass

    # fixedsections = [(addr, sectioninfo), ...]
    fixedsections = []
    textsections = []
    rodatasections = []
    datasections = []

    # Find desired sections.
    for section in sections:
        name = section[1]
        if name[:11] == '.fixedaddr.':
            addr = int(name[11:], 16)
            fixedsections.append((addr, section))
        if name[:6] == '.text.':
            textsections.append(section)
        if name[:17] == '.rodata.__func__.' or name == '.rodata.str1.1':
            rodatasections.append(section)
        if name[:8] == '.data16.':
            datasections.append(section)

    # Write regular sections
    for section in textsections:
        name = section[1]
        sys.stdout.write("*(%s)\n" % (name,))
    sys.stdout.write("code16_rodata = . ;\n")
    for section in rodatasections:
        name = section[1]
        sys.stdout.write("*(%s)\n" % (name,))
    for section in datasections:
        name = section[1]
        sys.stdout.write("*(%s)\n" % (name,))

    # Write fixed sections
    sys.stdout.write("freespace1_start = . ;\n")
    first = 1
    for addr, section in fixedsections:
        name = section[1]
        sys.stdout.write(". = ( 0x%x - code16_start ) ;\n" % (addr,))
        if first:
            first = 0
            sys.stdout.write("freespace1_end = . ;\n")
        sys.stdout.write("*(%s)\n" % (name,))

if __name__ == '__main__':
    main()
