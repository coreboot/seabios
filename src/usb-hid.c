// Code for handling USB Human Interface Devices (HID).
//
// Copyright (C) 2009  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "util.h" // dprintf
#include "usb-hid.h" // usb_keyboard_setup
#include "config.h" // CONFIG_*
#include "usb.h" // usb_ctrlrequest
#include "biosvar.h" // GET_GLOBAL

struct usb_pipe *keyboard_pipe VAR16VISIBLE;


/****************************************************************
 * Setup
 ****************************************************************/

static int
set_protocol(struct usb_pipe *pipe, u16 val)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    req.bRequest = HID_REQ_SET_PROTOCOL;
    req.wValue = val;
    req.wIndex = 0;
    req.wLength = 0;
    return send_default_control(pipe, &req, NULL);
}

static int
set_idle(struct usb_pipe *pipe, int ms)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    req.bRequest = HID_REQ_SET_IDLE;
    req.wValue = (ms/4)<<8;
    req.wIndex = 0;
    req.wLength = 0;
    return send_default_control(pipe, &req, NULL);
}

#define KEYREPEATWAITMS 500
#define KEYREPEATMS 33

int
usb_keyboard_init(struct usb_pipe *pipe
                  , struct usb_interface_descriptor *iface, int imax)
{
    if (! CONFIG_USB_KEYBOARD)
        return -1;
    if (keyboard_pipe)
        // XXX - this enables the first found keyboard (could be random)
        return -1;
    dprintf(2, "usb_keyboard_setup %p\n", pipe);

    // Find intr in endpoint.
    struct usb_endpoint_descriptor *epdesc = findEndPointDesc(
        iface, imax, USB_ENDPOINT_XFER_INT, USB_DIR_IN);
    if (!epdesc || epdesc->wMaxPacketSize != 8) {
        dprintf(1, "No keyboard intr in?\n");
        return -1;
    }

    // Enable "boot" protocol.
    int ret = set_protocol(pipe, 1);
    if (ret)
        return -1;
    // Periodically send reports to enable key repeat.
    ret = set_idle(pipe, KEYREPEATMS);
    if (ret)
        return -1;

    keyboard_pipe = alloc_intr_pipe(pipe, epdesc);
    if (!keyboard_pipe)
        return -1;

    dprintf(1, "USB keyboard initialized\n");
    return 0;
}

void
usb_keyboard_setup(void)
{
    if (! CONFIG_USB_KEYBOARD)
        return;
    keyboard_pipe = NULL;
}


/****************************************************************
 * Keyboard events
 ****************************************************************/

static u16 KeyToScanCode[] VAR16 = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x001e, 0x0030, 0x002e, 0x0020,
    0x0012, 0x0021, 0x0022, 0x0023, 0x0017, 0x0024, 0x0025, 0x0026,
    0x0032, 0x0031, 0x0018, 0x0019, 0x0010, 0x0013, 0x001f, 0x0014,
    0x0016, 0x002f, 0x0011, 0x002d, 0x0015, 0x002c, 0x0002, 0x0003,
    0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000a, 0x000b,
    0x001c, 0x0001, 0x000e, 0x000f, 0x0039, 0x000c, 0x000d, 0x001a,
    0x001b, 0x002b, 0x0000, 0x0027, 0x0028, 0x0029, 0x0033, 0x0034,
    0x0035, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f, 0x0040,
    0x0041, 0x0042, 0x0043, 0x0044, 0x0057, 0x0058, 0xe037, 0x0046,
    0xe11d, 0xe052, 0xe047, 0xe049, 0xe053, 0xe04f, 0xe051, 0xe04d,
    0xe04b, 0xe050, 0xe048, 0x0045, 0xe035, 0x0037, 0x004a, 0x004e,
    0xe01c, 0x004f, 0x0050, 0x0051, 0x004b, 0x004c, 0x004d, 0x0047,
    0x0048, 0x0049, 0x0052, 0x0053
};

static u16 ModifierToScanCode[] VAR16 = {
    //lcntl, lshift, lalt, lgui, rcntl, rshift, ralt, rgui
    0x001d, 0x002a, 0x0038, 0xe05b, 0xe01d, 0x0036, 0xe038, 0xe05c
};

#define RELEASEBIT 0x80

struct keyevent {
    u8 modifiers;
    u8 reserved;
    u8 keys[6];
};

static void
prockeys(u16 keys)
{
    if (keys > 0xff) {
        u8 key = keys>>8;
        if (key == 0xe1) {
            // Pause key
            process_key(0xe1);
            process_key(0x1d | (keys & RELEASEBIT));
            process_key(0x45 | (keys & RELEASEBIT));
            return;
        }
        process_key(key);
    }
    process_key(keys);
}

static void
procscankey(u8 key, u8 flags)
{
    if (key >= ARRAY_SIZE(KeyToScanCode))
        return;
    u16 keys = GET_GLOBAL(KeyToScanCode[key]);
    if (keys)
        prockeys(keys | flags);
}

static void
procmodkey(u8 mods, u8 flags)
{
    int i;
    for (i=0; mods; i++)
        if (mods & (1<<i)) {
            // Modifier key change.
            prockeys(GET_GLOBAL(ModifierToScanCode[i]) | flags);
            mods &= ~(1<<i);
        }
}

static void noinline
handle_key(struct keyevent *data)
{
    dprintf(9, "Got key %x %x\n", data->modifiers, data->keys[0]);

    // Load old keys.
    u16 ebda_seg = get_ebda_seg();
    struct usbkeyinfo old;
    old.data = GET_EBDA2(ebda_seg, usbkey_last.data);

    // Check for keys no longer pressed.
    int addpos = 0;
    int i;
    for (i=0; i<ARRAY_SIZE(old.keys); i++) {
        u8 key = old.keys[i];
        if (!key)
            break;
        int j;
        for (j=0;; j++) {
            if (j>=ARRAY_SIZE(data->keys)) {
                // Key released.
                procscankey(key, RELEASEBIT);
                if (i+1 >= ARRAY_SIZE(old.keys) || !old.keys[i+1])
                    // Last pressed key released - disable repeat.
                    old.repeatcount = 0xff;
                break;
            }
            if (data->keys[j] == key) {
                // Key still pressed.
                data->keys[j] = 0;
                old.keys[addpos++] = key;
                break;
            }
        }
    }
    procmodkey(old.modifiers & ~data->modifiers, RELEASEBIT);

    // Process new keys
    procmodkey(data->modifiers & ~old.modifiers, 0);
    old.modifiers = data->modifiers;
    for (i=0; i<ARRAY_SIZE(data->keys); i++) {
        u8 key = data->keys[i];
        if (!key)
            continue;
        // New key pressed.
        procscankey(key, 0);
        old.keys[addpos++] = key;
        old.repeatcount = KEYREPEATWAITMS / KEYREPEATMS + 1;
    }
    if (addpos < ARRAY_SIZE(old.keys))
        old.keys[addpos] = 0;

    // Check for key repeat event.
    if (addpos) {
        if (!old.repeatcount)
            procscankey(old.keys[addpos-1], 0);
        else if (old.repeatcount != 0xff)
            old.repeatcount--;
    }

    // Update old keys
    SET_EBDA2(ebda_seg, usbkey_last.data, old.data);
}

void
usb_check_key(void)
{
    if (! CONFIG_USB_KEYBOARD)
        return;
    struct usb_pipe *pipe = GET_GLOBAL(keyboard_pipe);
    if (!pipe)
        return;

    for (;;) {
        struct keyevent data;
        int ret = usb_poll_intr(pipe, &data);
        if (ret)
            break;
        handle_key(&data);
    }
}
