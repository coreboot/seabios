// Support for handling the PS/2 mouse/keyboard ports.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Based on code Copyright (c) 1999-2004 Vojtech Pavlik
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "ioport.h" // inb
#include "util.h" // dprintf
#include "biosvar.h" // GET_EBDA
#include "ps2port.h" // kbd_command


/****************************************************************
 * Low level i8042 commands.
 ****************************************************************/

// Timeout value.
#define I8042_CTL_TIMEOUT       10000

#define I8042_BUFFER_SIZE       16

static int
i8042_wait_read(void)
{
    int i;
    for (i=0; i<I8042_CTL_TIMEOUT; i++) {
        u8 status = inb(PORT_PS2_STATUS);
        if (status & I8042_STR_OBF)
            return 0;
        udelay(50);
    }
    dprintf(1, "i8042 timeout on wait read\n");
    return -1;
}

static int
i8042_wait_write(void)
{
    int i;
    for (i=0; i<I8042_CTL_TIMEOUT; i++) {
        u8 status = inb(PORT_PS2_STATUS);
        if (! (status & I8042_STR_IBF))
            return 0;
        udelay(50);
    }
    dprintf(1, "i8042 timeout on wait write\n");
    return -1;
}

int
i8042_flush(void)
{
    unsigned long flags = irq_save();

    int i;
    for (i=0; i<I8042_BUFFER_SIZE; i++) {
        u8 status = inb(PORT_PS2_STATUS);
        if (! (status & I8042_STR_OBF)) {
            irq_restore(flags);
            return 0;
        }
        udelay(50);
        inb(PORT_PS2_DATA);
    }

    irq_restore(flags);
    dprintf(1, "i8042 timeout on flush\n");
    return -1;
}

static int
__i8042_command(int command, u8 *param)
{
    int receive = (command >> 8) & 0xf;
    int send = (command >> 12) & 0xf;

    // Send the command.
    int ret = i8042_wait_write();
    if (ret)
        return ret;
    outb(command, PORT_PS2_STATUS);

    // Send parameters (if any).
    int i;
    for (i = 0; i < send; i++) {
        ret = i8042_wait_write();
        if (ret)
            return ret;
        outb(param[i], PORT_PS2_DATA);
    }

    // Receive parameters (if any).
    for (i = 0; i < receive; i++) {
        ret = i8042_wait_read();
        if (ret)
            return ret;
        param[i] = inb(PORT_PS2_DATA);
    }

    return 0;
}

int
i8042_command(int command, u8 *param)
{
    unsigned long flags = irq_save();
    int ret = __i8042_command(command, param);
    irq_restore(flags);
    if (ret)
        dprintf(2, "i8042 command %x failed\n", command);
    return ret;
}

static int
i8042_kbd_write(u8 c)
{
    unsigned long flags = irq_save();

    int ret = i8042_wait_write();
    if (! ret)
        outb(c, PORT_PS2_DATA);

    irq_restore(flags);

    return ret;
}

static int
i8042_aux_write(u8 c)
{
    return i8042_command(I8042_CMD_AUX_SEND, &c);
}


/****************************************************************
 * Device commands.
 ****************************************************************/

#define PS2_RET_ACK             0xfa
#define PS2_RET_NAK             0xfe

static int
ps2_sendbyte(int aux, u8 command)
{
    int ret;
    if (aux)
        ret = i8042_aux_write(command);
    else
        ret = i8042_kbd_write(command);
    if (ret)
        return ret;

    // Read ack.
    ret = i8042_wait_read();
    if (ret)
        return ret;
    u8 ack = inb(PORT_PS2_DATA);
    if (ack != PS2_RET_ACK) {
        dprintf(1, "Missing ack (got %x not %x)\n", ack, PS2_RET_ACK);
        return -1;
    }

    return 0;
}

static int
ps2_command(int aux, int command, u8 *param)
{
    int ret2;
    int receive = (command >> 8) & 0xf;
    int send = (command >> 12) & 0xf;

    // Disable interrupts and keyboard/mouse.
    u8 ps2ctr = GET_EBDA(ps2ctr);
    u8 newctr = ps2ctr;
    if (aux)
        newctr |= I8042_CTR_KBDDIS;
    else
        newctr |= I8042_CTR_AUXDIS;
    newctr &= ~(I8042_CTR_KBDINT|I8042_CTR_AUXINT);
    dprintf(6, "i8042 ctr old=%x new=%x\n", ps2ctr, newctr);
    int ret = i8042_command(I8042_CMD_CTL_WCTR, &newctr);
    if (ret)
        return ret;

    // Send command.
    ret = ps2_sendbyte(aux, command);
    if (ret)
        goto fail;

    // Send parameters (if any).
    int i;
    for (i = 0; i < send; i++) {
        ret = ps2_sendbyte(aux, command);
        if (ret)
            goto fail;
    }

    // Receive parameters (if any).
    for (i = 0; i < receive; i++) {
        ret = i8042_wait_read();
        if (ret) {
            // On a receive timeout, return the item number that the
            // transfer failed on.
            ret = i + 1;
            goto fail;
        }
        param[i] = inb(PORT_PS2_DATA);
    }

fail:
    // Restore interrupts and keyboard/mouse.
    ret2 = i8042_command(I8042_CMD_CTL_WCTR, &ps2ctr);
    if (ret2)
        return ret2;

    return ret;
}

int
kbd_command(int command, u8 *param)
{
    int ret = ps2_command(0, command, param);
    if (ret)
        dprintf(2, "keyboard command %x failed (ret=%d)\n", command, ret);
    return ret;
}

int
aux_command(int command, u8 *param)
{
    int ret = ps2_command(1, command, param);
    if (ret)
        dprintf(2, "mouse command %x failed (ret=%d)\n", command, ret);
    return ret;
}
