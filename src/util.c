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

void *
memset(void *s, int c, size_t n)
{
    while (n)
        ((char *)s)[--n] = c;
    return s;
}

void *
memcpy(void *far_d1, const void *far_s1, size_t len)
{
    u8 *d = far_d1;
    u8 *s = (u8*)far_s1;

    while (len--) {
        SET_FARPTR(*d, GET_FARPTR(*s));
        d++;
        s++;
    }

    return far_d1;
}

void
__set_fail(const char *fname, struct bregs *regs)
{
    __debug_fail(fname, regs);
    set_cf(regs, 1);
}

void
__set_code_fail(const char *fname, struct bregs *regs, u8 code)
{
    __set_fail(fname, regs);
    regs->ah = code;
}
