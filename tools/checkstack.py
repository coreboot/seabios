#!/usr/bin/env python
# Script that tries to find how much stack space each function in an
# object is using.
#
# Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

# Usage:
#   objdump -m i386 -M i8086 -M suffix -d out/rom16.o | tools/checkstack.py

import sys
import re

# List of functions we can assume are never called.
#IGNORE = ['screenc', 'BX_PANIC', '__dprintf']
IGNORE = ['screenc', 'BX_PANIC']

# Find out maximum stack usage for a function
def calcmaxstack(funcs, func):
    info = funcs[func]
    if func.split('.')[0] in IGNORE:
        # Function is hardcoded to report 0 stack usage
        info[1] = 0
        return
    # Find max of all nested calls.
    max = info[0]
    info[1] = max
    for addr, callfname, usage in info[2]:
        callinfo = funcs[callfname]
        if callinfo[1] is None:
            calcmaxstack(funcs, callfname)
        totusage = usage + callinfo[1]
        if totusage > max:
            max = totusage
    info[1] = max

hex_s = r'[0-9a-f]+'
re_func = re.compile(r'^' + hex_s + r' <(?P<func>.*)>:$')
re_asm = re.compile(
    r'^[ ]*(?P<addr>' + hex_s + r'):\t.*\t'
    r'(addr32 )?(?P<insn>[a-z0-9]+ [^<]*)( <(?P<ref>.*)>)?$')
re_usestack = re.compile(
    r'^(push.*)|(sub.* [$](?P<num>0x' + hex_s + r'),%esp)$')

def calc():
    # funcs = {funcname: [basicstackusage, maxstackusage
    #                     , [(addr, callfname, stackusage), ...]] }
    funcs = {'<indirect>': [0, 0, []]}
    cur = None
    atstart = 0
    stackusage = 0

    # Parse input lines
    for line in sys.stdin.readlines():
        m = re_func.match(line)
        if m is not None:
            # Found function
            cur = [0, None, []]
            funcs[m.group('func')] = cur
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
                cur[0] = stackusage
                atstart = 0

            ref = m.group('ref')
            if ref is None:
                if insn[:6] == 'lcallw':
                    stackusage += 4
                    ref = '<indirect>'
                else:
                    # misc instruction - just ignore
                    continue
            else:
                # Jump or call insn
                if '+' in ref:
                    # Inter-function jump - reset stack usage to
                    # preamble usage
                    stackusage = cur[0]
                    continue
                if ref.split('.')[0] in IGNORE:
                    # Call ignored - list only for informational purposes
                    stackusage = 0
                elif insn[:1] == 'j':
                    # Tail call
                    stackusage = 0
                elif insn[:5] == 'calll':
                    stackusage += 4
                else:
                    print "unknown call", ref
            if (ref, stackusage) not in subfuncs:
                cur[2].append((m.group('addr'), ref, stackusage))
                subfuncs[(ref, stackusage)] = 1
            # Reset stack usage to preamble usage
            stackusage = cur[0]

            continue

        #print "other", repr(line)

    # Calculate maxstackusage
    for func, info in funcs.items():
        if info[1] is not None:
            continue
        calcmaxstack(funcs, func)

    # Show all functions
    funcnames = funcs.keys()
    funcnames.sort()
    for func in funcnames:
        basicusage, maxusage, calls = funcs[func]
        if maxusage == 0:
            continue
        print "\n%s[%d,%d]:" % (func, basicusage, maxusage)
        for addr, callfname, stackusage in calls:
            callinfo = funcs[callfname]
            print "    %04s:%-40s [%d+%d,%d]" % (
                addr, callfname, stackusage, callinfo[0], stackusage+callinfo[1])

def main():
    calc()

if __name__ == '__main__':
    main()
