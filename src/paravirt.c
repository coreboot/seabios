// Paravirtualization support.
//
// Copyright (C) 2009 Red Hat Inc.
//
// Authors:
//  Gleb Natapov <gnatapov@redhat.com>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "config.h"
#include "ioport.h"
#include "paravirt.h"

int qemu_cfg_present;

static void
qemu_cfg_select(u16 f)
{
    outw(f, PORT_QEMU_CFG_CTL);
}

static void
qemu_cfg_read(u8 *buf, int len)
{
    while (len--)
        *(buf++) = inb(PORT_QEMU_CFG_DATA);
}

static void
qemu_cfg_read_entry(void *buf, int e, int len)
{
    qemu_cfg_select(e);
    qemu_cfg_read(buf, len);
}

void qemu_cfg_port_probe(void)
{
    char *sig = "QEMU";
    int i;

    if (CONFIG_COREBOOT)
        return;

    qemu_cfg_present = 1;

    qemu_cfg_select(QEMU_CFG_SIGNATURE);

    for (i = 0; i < 4; i++)
        if (inb(PORT_QEMU_CFG_DATA) != sig[i]) {
            qemu_cfg_present = 0;
            break;
        }
    dprintf(4, "qemu_cfg_present=%d\n", qemu_cfg_present);
}

void qemu_cfg_get_uuid(u8 *uuid)
{
    if (!qemu_cfg_present)
        return;

    qemu_cfg_read_entry(uuid, QEMU_CFG_UUID, 16);
}

int qemu_cfg_show_boot_menu(void)
{
    u16 v;
    if (!qemu_cfg_present)
        return 1;

    qemu_cfg_read_entry(&v, QEMU_CFG_BOOT_MENU, sizeof(v));

    return v;
}

