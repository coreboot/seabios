// Menu presented during final phase of "post".
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "biosvar.h" // GET_EBDA
#include "util.h" // mdelay
#include "bregs.h" // struct bregs

static int
check_for_keystroke()
{
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ah = 1;
    call16_int(0x16, &br);
    return !(br.flags & F_ZF);
}

static int
get_keystroke()
{
    struct bregs br;
    memset(&br, 0, sizeof(br));
    call16_int(0x16, &br);
    return br.ah;
}

static void
usleep(u32 usec)
{
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ah = 0x86;
    br.cx = usec >> 16;
    br.dx = usec;
    call16_int(0x15, &br);
}

static int
timed_check_for_keystroke(int msec)
{
    while (msec > 0) {
        if (check_for_keystroke())
            return 1;
        usleep(50*1000);
        msec -= 50;
    }
    return 0;
}

void
interactive_bootmenu()
{
    if (! CONFIG_BOOTMENU)
        return;

    while (check_for_keystroke())
        get_keystroke();

    printf("Press F12 for boot menu.\n\n");

    if (!timed_check_for_keystroke(2500))
        return;
    int scan_code = get_keystroke();
    if (scan_code != 0x86)
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
            // Add user choice to the boot order.
            u16 choice = scan_code - 1;
            u32 bootorder = GET_EBDA(ipl.bootorder);
            SET_EBDA(ipl.bootorder, (bootorder << 4) | choice);
            break;
        }
    }
    printf("\n");
}
