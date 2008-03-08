#include "util.h" // usleep

// Sleep for n microseconds. currently using the
// refresh request port 0x61 bit4, toggling every 15usec
void
usleep(u32 count)
{
    count = count / 15;
    u8 kbd = inb(PORT_PS2_CTRLB);
    while (count)
        if ((inb(PORT_PS2_CTRLB) ^ kbd) & KBD_REFRESH)
            count--;
}
