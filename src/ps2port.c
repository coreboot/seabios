// Support for handling the PS/2 mouse/keyboard ports.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Several ideas taken from code Copyright (c) 1999-2004 Vojtech Pavlik
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "ioport.h" // inb
#include "util.h" // dprintf
#include "biosvar.h" // GET_EBDA
#include "ps2port.h" // kbd_command
#include "pic.h" // eoi_pic1


/****************************************************************
 * Low level i8042 commands.
 ****************************************************************/

// Timeout value.
#define I8042_CTL_TIMEOUT       10000

#define I8042_BUFFER_SIZE       16

static int
i8042_wait_read(void)
{
    dprintf(7, "i8042_wait_read\n");
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
    dprintf(7, "i8042_wait_write\n");
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
    dprintf(7, "i8042_flush\n");
    int i;
    for (i=0; i<I8042_BUFFER_SIZE; i++) {
        u8 status = inb(PORT_PS2_STATUS);
        if (! (status & I8042_STR_OBF))
            return 0;
        udelay(50);
        u8 data = inb(PORT_PS2_DATA);
        dprintf(7, "i8042 flushed %x (status=%x)\n", data, status);
    }

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
        dprintf(7, "i8042 param=%x\n", param[i]);
    }

    return 0;
}

int
i8042_command(int command, u8 *param)
{
    dprintf(7, "i8042_command cmd=%x\n", command);
    int ret = __i8042_command(command, param);
    if (ret)
        dprintf(2, "i8042 command %x failed\n", command);
    return ret;
}

static int
i8042_kbd_write(u8 c)
{
    dprintf(7, "i8042_kbd_write c=%d\n", c);
    int ret = i8042_wait_write();
    if (! ret)
        outb(c, PORT_PS2_DATA);
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

static void
process_ps2byte(u8 status, u8 data)
{
    if (!MODE16) {
        // Don't pull in all of keyboard/mouse code into 32bit code -
        // just discard the data.
        dprintf(1, "Discarding ps2 data %x (status=%x)\n", data, status);
        return;
    }
    if (status & I8042_STR_AUXDATA)
        process_mouse(data);
    else
        process_key(data);
}

static int
ps2_recvbyte(int aux, int needack, int timeout)
{
    u64 end = calc_future_tsc(timeout);
    for (;;) {
        u8 status = inb(PORT_PS2_STATUS);
        if (status & I8042_STR_OBF) {
            u8 data = inb(PORT_PS2_DATA);
            dprintf(7, "ps2 read %x\n", data);

            if (!!(status & I8042_STR_AUXDATA) == aux) {
                if (!needack)
                    return data;
                if (data == PS2_RET_ACK)
                    return data;
                if (data == PS2_RET_NAK) {
                    dprintf(1, "Got ps2 nak (status=%x)\n", status);
                    return data;
                }
            }

            // Data not part of this command.
            process_ps2byte(status, data);
        }

        if (check_time(end)) {
            dprintf(1, "ps2_recvbyte timeout\n");
            return -1;
        }
        yield();
    }
}

static int
ps2_sendbyte(int aux, u8 command, int timeout)
{
    dprintf(7, "ps2_sendbyte aux=%d cmd=%x\n", aux, command);
    int ret;
    if (aux)
        ret = i8042_aux_write(command);
    else
        ret = i8042_kbd_write(command);
    if (ret)
        return ret;

    // Read ack.
    ret = ps2_recvbyte(aux, 1, timeout);
    if (ret < 0)
        return ret;
    if (ret != PS2_RET_ACK)
        return -1;

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

    if (command == ATKBD_CMD_RESET_BAT) {
        // Reset is special wrt timeouts.

        // Send command.
        ret = ps2_sendbyte(aux, command, 1000);
        if (ret)
            goto fail;

        // Receive parameters.
        ret = ps2_recvbyte(aux, 0, 4000);
        if (ret < 0)
            goto fail;
        param[0] = ret;
        ret = ps2_recvbyte(aux, 0, 100);
        if (ret < 0)
            // Some devices only respond with one byte on reset.
            ret = 0;
        param[1] = ret;
    } else {
        // Send command.
        ret = ps2_sendbyte(aux, command, 200);
        if (ret)
            goto fail;

        // Send parameters (if any).
        int i;
        for (i = 0; i < send; i++) {
            ret = ps2_sendbyte(aux, param[i], 200);
            if (ret)
                goto fail;
        }

        // Receive parameters (if any).
        for (i = 0; i < receive; i++) {
            ret = ps2_recvbyte(aux, 0, 500);
            if (ret < 0)
                goto fail;
            param[i] = ret;
        }
    }

    ret = 0;

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
    dprintf(7, "kbd_command cmd=%x\n", command);
    int ret = ps2_command(0, command, param);
    if (ret)
        dprintf(2, "keyboard command %x failed\n", command);
    return ret;
}

int
aux_command(int command, u8 *param)
{
    dprintf(7, "aux_command cmd=%x\n", command);
    int ret = ps2_command(1, command, param);
    if (ret)
        dprintf(2, "mouse command %x failed\n", command);
    return ret;
}


/****************************************************************
 * IRQ handlers
 ****************************************************************/

static void
process_ps2irq(void)
{
    u8 status = inb(PORT_PS2_STATUS);
    if (!(status & I8042_STR_OBF)) {
        dprintf(1, "ps2 irq but no data.\n");
        return;
    }
    u8 data = inb(PORT_PS2_DATA);

    process_ps2byte(status, data);
}

// INT74h : PS/2 mouse hardware interrupt
void VISIBLE16
handle_74(void)
{
    if (! CONFIG_PS2PORT)
        return;

    debug_isr(DEBUG_ISR_74);
    process_ps2irq();
    eoi_pic2();
}

// INT09h : Keyboard Hardware Service Entry Point
void VISIBLE16
handle_09(void)
{
    if (! CONFIG_PS2PORT)
        return;

    debug_isr(DEBUG_ISR_09);
    process_ps2irq();
    eoi_pic1();
}


/****************************************************************
 * Setup
 ****************************************************************/

static void
keyboard_init(void *data)
{
    /* flush incoming keys */
    int ret = i8042_flush();
    if (ret)
        return;

    // Controller self-test.
    u8 param[2];
    ret = i8042_command(I8042_CMD_CTL_TEST, param);
    if (ret)
        return;
    if (param[0] != 0x55) {
        dprintf(1, "i8042 self test failed (got %x not 0x55)\n", param[0]);
        return;
    }

    // Controller keyboard test.
    ret = i8042_command(I8042_CMD_KBD_TEST, param);
    if (ret)
        return;
    if (param[0] != 0x00) {
        dprintf(1, "i8042 keyboard test failed (got %x not 0x00)\n", param[0]);
        return;
    }

    // Enable keyboard and mouse ports.
    ret = i8042_command(I8042_CMD_KBD_ENABLE, NULL);
    if (ret)
        return;
    ret = i8042_command(I8042_CMD_AUX_ENABLE, NULL);
    if (ret)
        return;


    /* ------------------- keyboard side ------------------------*/
    /* reset keyboard and self test  (keyboard side) */
    ret = kbd_command(ATKBD_CMD_RESET_BAT, param);
    if (ret)
        return;
    if (param[0] != 0xaa) {
        dprintf(1, "keyboard self test failed (got %x not 0xaa)\n", param[0]);
        return;
    }

    /* Disable keyboard */
    ret = kbd_command(ATKBD_CMD_RESET_DIS, NULL);
    if (ret)
        return;

    // Set scancode command (mode 2)
    param[0] = 0x02;
    ret = kbd_command(ATKBD_CMD_SSCANSET, param);
    if (ret)
        return;

    // Keyboard Mode: scan code convert, disable mouse, enable IRQ 1
    SET_EBDA(ps2ctr, I8042_CTR_AUXDIS | I8042_CTR_XLATE | I8042_CTR_KBDINT);

    /* Enable keyboard */
    ret = kbd_command(ATKBD_CMD_ENABLE, NULL);
    if (ret)
        return;

    dprintf(1, "keyboard initialized\n");
}

void
ps2port_setup(void)
{
    if (! CONFIG_PS2PORT)
        return;
    dprintf(3, "init ps2port\n");

    enable_hwirq(1, entry_09);
    enable_hwirq(12, entry_74);

    run_thread(keyboard_init, NULL);
}
