#include "util.h" // usleep

// Sum the bytes in the specified area.
u8
checksum(u8 *far_data, u32 len)
{
    u32 i;
    u8 sum = 0;
    for (i=0; i<len; i++)
        sum += GET_FARPTR(far_data[i]);
    return sum;
}

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
