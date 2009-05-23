#!/usr/bin/env python
# Script to arrange sections to ensure fixed offsets.
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

import sys

def main():
    # Get output name
    outname = sys.argv[1]

    # Read in section names and sizes
    # sections = [(size, align, name), ...]
    sections = []
    for line in sys.stdin.readlines():
        try:
            idx, name, size, vma, lma, fileoff, align = line.split()
            if align[:3] != '2**':
                continue
            sections.append((int(size, 16), 2**int(align[3:]), name))
        except:
            pass

    doLayout(sections, outname)

def alignpos(pos, alignbytes):
    mask = alignbytes - 1
    return (pos + mask) & ~mask

MAXPOS = 0x10000

def outsection(file, name):
    file.write("*(%s)\n" % (name,))

def doLayout(sections, outname):
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
        if name == rodatasections[0][2]:
            output.write("code16_rodata = . ;\n")
        outsection(output, name)

    # Write fixed sections
    for addr, section, extrasections in fixedsections:
        name = section[2]
        output.write(". = ( 0x%x - code16_start ) ;\n" % (addr,))
        output.write("*(%s)\n" % (name,))
        for extrasection in extrasections:
            outsection(output, extrasection[2])

    # Write trailer
    output.write("""
                code16_end = ABSOLUTE(.) ;
        }
""")


if __name__ == '__main__':
    main()
