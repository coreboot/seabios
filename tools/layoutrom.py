#!/usr/bin/env python
# Script to arrange sections to ensure fixed offsets.
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

import sys


def alignpos(pos, alignbytes):
    mask = alignbytes - 1
    return (pos + mask) & ~mask


######################################################################
# 16bit fixed address section fitting
######################################################################

MAXPOS = 0x10000

def doLayout16(sections, outname):
    textsections = []
    rodatasections = []
    datasections = []
    # fixedsections = [(addr, sectioninfo, extasectionslist), ...]
    fixedsections = []
    # canrelocate = [(sectioninfo, list), ...]
    canrelocate = []

    # Find desired sections.
    for section in sections:
        size, align, name = section
        if name[:11] == '.fixedaddr.':
            addr = int(name[11:], 16)
            fixedsections.append((addr, section, []))
            if align != 1:
                print "Error: Fixed section %s has non-zero alignment (%d)" % (
                    name, align)
                sys.exit(1)
        if name[:6] == '.text.':
            textsections.append(section)
            canrelocate.append((section, textsections))
        if name[:17] == '.rodata.__func__.' or name == '.rodata.str1.1':
            rodatasections.append(section)
            #canrelocate.append((section, rodatasections))
        if name[:8] == '.data16.':
            datasections.append(section)
            #canrelocate.append((section, datasections))

    # Find freespace in fixed address area
    fixedsections.sort()
    # fixedAddr = [(freespace, sectioninfo), ...]
    fixedAddr = []
    for i in range(len(fixedsections)):
        fixedsectioninfo = fixedsections[i]
        addr, section, extrasectionslist = fixedsectioninfo
        if i == len(fixedsections) - 1:
            nextaddr = MAXPOS
        else:
            nextaddr = fixedsections[i+1][0]
        avail = nextaddr - addr - section[0]
        fixedAddr.append((avail, fixedsectioninfo))

    # Attempt to fit other sections into fixed area
    fixedAddr.sort()
    canrelocate.sort()
    totalused = 0
    for freespace, fixedsectioninfo in fixedAddr:
        fixedaddr, fixedsection, extrasections = fixedsectioninfo
        addpos = fixedaddr + fixedsection[0]
        totalused += fixedsection[0]
        nextfixedaddr = addpos + freespace
#        print "Filling section %x uses %d, next=%x, available=%d" % (
#            fixedaddr, fixedsection[0], nextfixedaddr, freespace)
        while 1:
            canfit = None
            for fixedaddrinfo in canrelocate:
                fitsection, inlist = fixedaddrinfo
                fitsize, fitalign, fitname = fitsection
                if addpos + fitsize > nextfixedaddr:
                    # Can't fit and nothing else will fit.
                    break
                fitnextaddr = alignpos(addpos, fitalign) + fitsize
#                print "Test %s - %x vs %x" % (
#                    fitname, fitnextaddr, nextfixedaddr)
                if fitnextaddr > nextfixedaddr:
                    # This item can't fit.
                    continue
                canfit = (fitnextaddr, fixedaddrinfo)
            if canfit is None:
                break
            # Found a section that can fit.
            fitnextaddr, fixedaddrinfo = canfit
            canrelocate.remove(fixedaddrinfo)
            fitsection, inlist = fixedaddrinfo
            inlist.remove(fitsection)
            extrasections.append(fitsection)
            addpos = fitnextaddr
            totalused += fitsection[0]
#            print "    Adding %s (size %d align %d) pos=%x avail=%d" % (
#                fitsection[2], fitsection[0], fitsection[1]
#                , fitnextaddr, nextfixedaddr - fitnextaddr)
    firstfixed = fixedsections[0][0]

    # Find overall start position
    restalign = 0
    restspace = 0
    restsections = []
    for section in textsections + rodatasections + datasections:
        size, align, name = section
        if align > restalign:
            restalign = align
        restspace = alignpos(restspace, align) + size
        restsections.append(section)
    startrest = (firstfixed - restspace) / restalign * restalign

    # Report stats
    total = MAXPOS-firstfixed
    slack = total - totalused
    print ("Fixed space: 0x%x-0x%x  total: %d  slack: %d"
           "  Percent slack: %.1f%%" % (
            firstfixed, MAXPOS, total, slack,
            (float(slack) / total) * 100.0))

    # Write header
    output = open(outname, 'wb')
    output.write("""
        .text16 0x%x : {
                code16_start = ABSOLUTE(.) ;
                freespace_end = . ;
""" % startrest)

    # Write regular sections
    for section in restsections:
        name = section[2]
        if rodatasections and name == rodatasections[0][2]:
            output.write("code16_rodata = . ;\n")
        output.write("*(%s)\n" % (name,))

    # Write fixed sections
    for addr, section, extrasections in fixedsections:
        name = section[2]
        output.write(". = ( 0x%x - code16_start ) ;\n" % (addr,))
        output.write("*(%s)\n" % (name,))
        for extrasection in extrasections:
            output.write("*(%s)\n" % (extrasection[2],))

    # Write trailer
    output.write("""
                code16_end = ABSOLUTE(.) ;
        }
""")


######################################################################
# 32bit section outputting
######################################################################

def outsections(file, sections, prefix):
    lp = len(prefix)
    for size, align, name in sections:
        if name[:lp] == prefix:
            file.write("*(%s)\n" % (name,))

def doLayout32(sections, outname):
    output = open(outname, 'wb')
    outsections(output, sections, '.text.')
    output.write("code32_rodata = . ;\n")
    outsections(output, sections, '.rodata')
    outsections(output, sections, '.data.')
    outsections(output, sections, '.bss.')


######################################################################
# Section garbage collection
######################################################################

def keepsection(name, pri, alt):
    if name in pri[3]:
        # Already kept - nothing to do.
        return
    pri[3].append(name)
    relocs = pri[2].get(name)
    if relocs is None:
        return
    # Keep all sections that this section points to
    for symbol in relocs:
        section = pri[1].get(symbol)
        if section is not None and section[:9] != '.discard.':
            keepsection(section, pri, alt)
            continue
        # Not in primary sections - it may be a cross 16/32 reference
        section = alt[1].get(symbol)
        if section is not None:
            keepsection(section, alt, pri)

def gc(info16, info32):
    # pri = (sections, symbols, relocs, keep sections)
    pri = (info16[0], info16[1], info16[2], [])
    alt = (info32[0], info32[1], info32[2], [])
    # Start by keeping sections that are globally visible.
    for size, align, section in info16[0]:
        if section[:11] == '.fixedaddr.' or '.export.' in section:
            keepsection(section, pri, alt)
    # Return sections found.
    sections16 = []
    for info in info16[0]:
        size, align, section = info
        if section not in pri[3]:
#            print "gc16", section
            continue
        sections16.append(info)
    sections32 = []
    for info in info32[0]:
        size, align, section = info
        if section not in alt[3]:
#            print "gc32", section
            continue
        sections32.append(info)
    return sections16, sections32


######################################################################
# Startup and input parsing
######################################################################

# Read in output from objdump
def parseObjDump(file):
    # sections = [(size, align, section), ...]
    sections = []
    # symbols[symbol] = section
    symbols = {}
    # relocs[section] = [symbol, ...]
    relocs = {}

    state = None
    for line in file.readlines():
        line = line.rstrip()
        if line == 'Sections:':
            state = 'section'
            continue
        if line == 'SYMBOL TABLE:':
            state = 'symbol'
            continue
        if line[:24] == 'RELOCATION RECORDS FOR [':
            state = 'reloc'
            relocsection = line[24:-2]
            continue

        if state == 'section':
            try:
                idx, name, size, vma, lma, fileoff, align = line.split()
                if align[:3] != '2**':
                    continue
                sections.append((int(size, 16), 2**int(align[3:]), name))
            except:
                pass
            continue
        if state == 'symbol':
            try:
                section, off, symbol = line[17:].split()
                off = int(off, 16)
                if '*' not in section:
                    symbols[symbol] = section
            except:
                pass
            continue
        if state == 'reloc':
            try:
                off, type, symbol = line.split()
                off = int(off, 16)
                relocs.setdefault(relocsection, []).append(symbol)
            except:
                pass
    return sections, symbols, relocs

def main():
    # Get output name
    in16, in32, out16, out32 = sys.argv[1:]

    infile16 = open(in16, 'rb')
    infile32 = open(in32, 'rb')

    info16 = parseObjDump(infile16)
    info32 = parseObjDump(infile32)

    sections16, sections32 = gc(info16, info32)

    doLayout16(sections16, out16)
    doLayout32(sections32, out32)

if __name__ == '__main__':
    main()
