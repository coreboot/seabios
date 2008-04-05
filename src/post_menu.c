// Menu presented during final phase of "post".
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "biosvar.h" // GET_EBDA
#include "util.h" // usleep

static u8
check_for_keystroke()
{
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ah = 1;
    call16_int(0x16, &br);
    return !(br.flags & F_ZF);
}

static u8
get_keystroke()
{
    struct bregs br;
    memset(&br, 0, sizeof(br));
    call16_int(0x16, &br);
    return br.ah;
}

static void
udelay_and_check_for_keystroke(u32 usec, int count)
{
    int i;
    for (i = 1; i <= count; i++) {
        usleep(usec);
        if (check_for_keystroke())
            break;
    }
}

void
interactive_bootmenu()
{
    while (check_for_keystroke())
        get_keystroke();

    printf("Press F12 for boot menu.\n\n");

    udelay_and_check_for_keystroke(500000, 5);
    if (! check_for_keystroke())
        return;
    u8 scan_code = get_keystroke();
    if (scan_code != 0x58)
        /* not F12 */
        return;

    while (check_for_keystroke())
        get_keystroke();

    printf("Select boot device:\n\n");

    int count = GET_EBDA(ipl.count);
    int i;
    for (i = 0; i < count; i++) {
        printf("%d. ", i+1);
        printf_bootdev(i);
        printf("\n");
    }

    for (;;) {
        scan_code = get_keystroke();
        if (scan_code == 0x01 || scan_code == 0x58)
            /* ESC or F12 */
            break;
        if (scan_code <= count + 1) {
            SET_EBDA(ipl.bootfirst, scan_code - 1);
            break;
        }
    }
    printf("\n");
}
