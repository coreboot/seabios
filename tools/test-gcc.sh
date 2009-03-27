#!/bin/sh
# Script to test if gcc's -combine option works properly.

mkdir -p out
TMPFILE1=out/tmp_testcompile1.c
TMPFILE1o=out/tmp_testcompile1.o
TMPFILE2=out/tmp_testcompile2.c
TMPFILE2o=out/tmp_testcompile2.o
TMPFILE3o=out/tmp_testcompile3.o

# Test for "-fwhole-program"
gcc -fwhole-program -S -o /dev/null -xc /dev/null > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "This version of gcc does not support -fwhole-program." > /dev/fd/2
    echo "Please upgrade to gcc v4.1 or later" > /dev/fd/2
    echo -1
    exit -1
fi

# Test if "visible" functions are marked global.
cat - > $TMPFILE1 <<EOF
void __attribute__((externally_visible)) t1() { }
EOF
$CC -c -fwhole-program $TMPFILE1 -o $TMPFILE1o > /dev/null 2>&1
cat - > $TMPFILE2 <<EOF
void t1();
void __attribute__((externally_visible)) main() { t1(); }
EOF
$CC -c -fwhole-program $TMPFILE2 -o $TMPFILE2o > /dev/null 2>&1
$CC $TMPFILE1o $TMPFILE2o -o $TMPFILE3o > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "  Working around non-global functions in -fwhole-program" > /dev/fd/2
fi

# Test if "-combine" works
mkdir -p out
cat - > $TMPFILE1 <<EOF
struct ts { union { int u1; struct { int u2; }; }; };
void t1(struct ts *r);
EOF
$CC -c -fwhole-program -combine $TMPFILE1 $TMPFILE1 -o $TMPFILE1o > /dev/null 2>&1
if [ $? -eq 0 ]; then
    #echo "  Setting AVOIDCOMBINE=0" > /dev/fd/2
    echo 0
else
    echo "  Enabling AVOIDCOMBINE=1" > /dev/fd/2
    echo 1
fi

rm -f $TMPFILE1 $TMPFILE1o $TMPFILE2 $TMPFILE2o $TMPFILE3o
