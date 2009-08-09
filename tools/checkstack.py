#!/usr/bin/env python
# Script that tries to find how much stack space each function in an
# object is using.
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

# Usage:
#   objdump -m i386 -M i8086 -M suffix -d out/rom16.reloc.o | tools/checkstack.py

import sys
import re

# List of functions we can assume are never called.
#IGNORE = ['panic', '__dprintf', '__send_disk_op']
IGNORE = ['panic', '__send_disk_op']

# Find out maximum stack usage for a function
def calcmaxstack(funcs, funcaddr):
    info = funcs[funcaddr]
    # Find max of all nested calls.
    max = info[1]
    info[2] = max
    for insnaddr, calladdr, usage in info[3]:
        callinfo = funcs[calladdr]
        if callinfo[2] is None:
            calcmaxstack(funcs, calladdr)
        if callinfo[0].split('.')[0] in IGNORE:
            # This called function is ignored - don't contribute it to
            # the max stack.
            continue
        totusage = usage + callinfo[2]
        if totusage > max:
            max = totusage
    info[2] = max

hex_s = r'[0-9a-f]+'
re_func = re.compile(r'^(?P<funcaddr>' + hex_s + r') <(?P<func>.*)>:$')
re_asm = re.compile(
    r'^[ ]*(?P<insnaddr>' + hex_s
    + r'):\t.*\t(addr32 )?(?P<insn>.+?)[ ]*((?P<calladdr>' + hex_s
    + r') <(?P<ref>.*)>)?$')
re_usestack = re.compile(
    r'^(push[f]?[lw])|(sub.* [$](?P<num>0x' + hex_s + r'),%esp)$')

def calc():
    # funcs[funcaddr] = [funcname, basicstackusage, maxstackusage
    #                    , [(addr, callfname, stackusage), ...]]
    funcs = {-1: ['<indirect>', 0, 0, []]}
    cur = None
    atstart = 0
    stackusage = 0

    # Parse input lines
    for line in sys.stdin.readlines():
        m = re_func.match(line)
        if m is not None:
            # Found function
            funcaddr = int(m.group('funcaddr'), 16)
            funcs[funcaddr] = cur = [m.group('func'), 0, None, []]
            stackusage = 0
            atstart = 1
            subfuncs = {}
            continue
        m = re_asm.match(line)
        if m is not None:
            insn = m.group('insn')

            im = re_usestack.match(insn)
            if im is not None:
                if insn[:5] == 'pushl' or insn[:6] == 'pushfl':
                    stackusage += 4
                    continue
                elif insn[:5] == 'pushw' or insn[:6] == 'pushfw':
                    stackusage += 2
                    continue
                stackusage += int(im.group('num'), 16)

            if atstart:
                if insn == 'movl   %esp,%ebp':
                    # Still part of initial header
                    continue
                cur[1] = stackusage
                atstart = 0

            calladdr = m.group('calladdr')
            if calladdr is None:
                if insn[:6] == 'lcallw':
                    stackusage += 4
                    calladdr = -1
                else:
                    # misc instruction - just ignore
                    continue
            else:
                # Jump or call insn
                calladdr = int(calladdr, 16)
                ref = m.group('ref')
                if '+' in ref:
                    # Inter-function jump - reset stack usage to
                    # preamble usage
                    stackusage = cur[1]
                    continue
                if insn[:1] == 'j':
                    # Tail call
                    stackusage = 0
                elif insn[:5] == 'calll':
                    stackusage += 4
                else:
                    print "unknown call", ref
            if (calladdr, stackusage) not in subfuncs:
                cur[3].append((m.group('insnaddr'), calladdr, stackusage))
                subfuncs[(calladdr, stackusage)] = 1
            # Reset stack usage to preamble usage
            stackusage = cur[1]

            continue

        #print "other", repr(line)

    # Calculate maxstackusage
    bynames = {}
    for funcaddr, info in funcs.items():
        bynames[info[0]] = info
        if info[2] is not None:
            continue
        calcmaxstack(funcs, funcaddr)

    # Show all functions
    funcnames = bynames.keys()
    funcnames.sort()
    for funcname in funcnames:
        name, basicusage, maxusage, calls = bynames[funcname]
        if maxusage == 0:
            continue
        print "\n%s[%d,%d]:" % (funcname, basicusage, maxusage)
        for insnaddr, calladdr, stackusage in calls:
            callinfo = funcs[calladdr]
            print "    %04s:%-40s [%d+%d,%d]" % (
                insnaddr, callinfo[0], stackusage, callinfo[1]
                , stackusage+callinfo[2])

def main():
    calc()

if __name__ == '__main__':
    main()
