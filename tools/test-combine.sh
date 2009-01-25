#!/bin/sh
# Script to test if gcc's -combine option works properly.

TMPFILE1=out/tmp_testcompile1.c
TMPFILE2=out/tmp_testcompile.o

mkdir -p out
cat - > $TMPFILE1 <<EOF
struct ts { union { int u1; struct { int u2; }; }; };
void t1(struct ts *r);
EOF

$CC -c -fwhole-program -combine $TMPFILE1 $TMPFILE1 -o $TMPFILE2 > /dev/null 2>&1

if [ $? -eq 0 ]; then
    #echo "  Setting AVOIDCOMBINE=0" > /dev/fd/2
    echo 0
else
    echo "  Enabling AVOIDCOMBINE=1" > /dev/fd/2
    echo 1
fi

rm -f $TMPFILE1 $TMPFILE2
