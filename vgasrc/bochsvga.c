#include "vgabios.h" // struct vbe_modeinfo
#include "vbe.h" // VBE_MODE_VESA_DEFINED
#include "bochsvga.h" // bochsvga_set_mode
#include "util.h" // dprintf
#include "config.h" // CONFIG_*
#include "biosvar.h" // SET_BDA
#include "stdvga.h" // VGAREG_SEQU_ADDRESS

static struct bochsvga_mode
{
    u16 mode;
    struct vgamode_s info;
} bochsvga_modes[] VAR16 = {
    /* standard modes */
    { 0x100, { MM_PACKED, 640,  400,  8  } },
    { 0x101, { MM_PACKED, 640,  480,  8  } },
    { 0x102, { MM_PLANAR, 800,  600,  4  } },
    { 0x103, { MM_PACKED, 800,  600,  8  } },
    { 0x104, { MM_PLANAR, 1024, 768,  4  } },
    { 0x105, { MM_PACKED, 1024, 768,  8  } },
    { 0x106, { MM_PLANAR, 1280, 1024, 4  } },
    { 0x107, { MM_PACKED, 1280, 1024, 8  } },
    { 0x10D, { MM_DIRECT, 320,  200,  15 } },
    { 0x10E, { MM_DIRECT, 320,  200,  16 } },
    { 0x10F, { MM_DIRECT, 320,  200,  24 } },
    { 0x110, { MM_DIRECT, 640,  480,  15 } },
    { 0x111, { MM_DIRECT, 640,  480,  16 } },
    { 0x112, { MM_DIRECT, 640,  480,  24 } },
    { 0x113, { MM_DIRECT, 800,  600,  15 } },
    { 0x114, { MM_DIRECT, 800,  600,  16 } },
    { 0x115, { MM_DIRECT, 800,  600,  24 } },
    { 0x116, { MM_DIRECT, 1024, 768,  15 } },
    { 0x117, { MM_DIRECT, 1024, 768,  16 } },
    { 0x118, { MM_DIRECT, 1024, 768,  24 } },
    { 0x119, { MM_DIRECT, 1280, 1024, 15 } },
    { 0x11A, { MM_DIRECT, 1280, 1024, 16 } },
    { 0x11B, { MM_DIRECT, 1280, 1024, 24 } },
    { 0x11C, { MM_PACKED, 1600, 1200, 8  } },
    { 0x11D, { MM_DIRECT, 1600, 1200, 15 } },
    { 0x11E, { MM_DIRECT, 1600, 1200, 16 } },
    { 0x11F, { MM_DIRECT, 1600, 1200, 24 } },
    /* BOCHS modes */
    { 0x140, { MM_DIRECT, 320,  200,  32 } },
    { 0x141, { MM_DIRECT, 640,  400,  32 } },
    { 0x142, { MM_DIRECT, 640,  480,  32 } },
    { 0x143, { MM_DIRECT, 800,  600,  32 } },
    { 0x144, { MM_DIRECT, 1024, 768,  32 } },
    { 0x145, { MM_DIRECT, 1280, 1024, 32 } },
    { 0x146, { MM_PACKED, 320,  200,  8  } },
    { 0x147, { MM_DIRECT, 1600, 1200, 32 } },
    { 0x148, { MM_PACKED, 1152, 864,  8  } },
    { 0x149, { MM_DIRECT, 1152, 864,  15 } },
    { 0x14a, { MM_DIRECT, 1152, 864,  16 } },
    { 0x14b, { MM_DIRECT, 1152, 864,  24 } },
    { 0x14c, { MM_DIRECT, 1152, 864,  32 } },
    { 0x178, { MM_DIRECT, 1280, 800,  16 } },
    { 0x179, { MM_DIRECT, 1280, 800,  24 } },
    { 0x17a, { MM_DIRECT, 1280, 800,  32 } },
    { 0x17b, { MM_DIRECT, 1280, 960,  16 } },
    { 0x17c, { MM_DIRECT, 1280, 960,  24 } },
    { 0x17d, { MM_DIRECT, 1280, 960,  32 } },
    { 0x17e, { MM_DIRECT, 1440, 900,  16 } },
    { 0x17f, { MM_DIRECT, 1440, 900,  24 } },
    { 0x180, { MM_DIRECT, 1440, 900,  32 } },
    { 0x181, { MM_DIRECT, 1400, 1050, 16 } },
    { 0x182, { MM_DIRECT, 1400, 1050, 24 } },
    { 0x183, { MM_DIRECT, 1400, 1050, 32 } },
    { 0x184, { MM_DIRECT, 1680, 1050, 16 } },
    { 0x185, { MM_DIRECT, 1680, 1050, 24 } },
    { 0x186, { MM_DIRECT, 1680, 1050, 32 } },
    { 0x187, { MM_DIRECT, 1920, 1200, 16 } },
    { 0x188, { MM_DIRECT, 1920, 1200, 24 } },
    { 0x189, { MM_DIRECT, 1920, 1200, 32 } },
    { 0x18a, { MM_DIRECT, 2560, 1600, 16 } },
    { 0x18b, { MM_DIRECT, 2560, 1600, 24 } },
    { 0x18c, { MM_DIRECT, 2560, 1600, 32 } },
};

#define BYTES_PER_PIXEL(m) ((GET_GLOBAL((m)->depth) + 7) / 8)

u32 pci_lfb_addr VAR16;

static inline u32 pci_config_readl(u16 bdf, u16 addr)
{
    int status;
    u32 val;

    addr &= ~3;

    asm volatile(
            "int $0x1a\n"
            "cli\n"
            "cld"
            : "=a"(status), "=c"(val)
            : "a"(0xb10a), "b"(bdf), "D"(addr)
            : "cc", "memory");

    if ((status >> 16))
        return (u32)-1;

    return val;
}


static u16 dispi_get_max_xres(void)
{
    u16 en;
    u16 xres;

    en = dispi_read(VBE_DISPI_INDEX_ENABLE);

    dispi_write(VBE_DISPI_INDEX_ENABLE, en | VBE_DISPI_GETCAPS);
    xres = dispi_read(VBE_DISPI_INDEX_XRES);
    dispi_write(VBE_DISPI_INDEX_ENABLE, en);

    return xres;
}

static u16 dispi_get_max_bpp(void)
{
    u16 en;
    u16 bpp;

    en = dispi_read(VBE_DISPI_INDEX_ENABLE);

    dispi_write(VBE_DISPI_INDEX_ENABLE, en | VBE_DISPI_GETCAPS);
    bpp = dispi_read(VBE_DISPI_INDEX_BPP);
    dispi_write(VBE_DISPI_INDEX_ENABLE, en);

    return bpp;
}

/* Called only during POST */
int
bochsvga_init(void)
{
    int ret = stdvga_init();
    if (ret)
        return ret;

    /* Sanity checks */
    dispi_write(VBE_DISPI_INDEX_ID, VBE_DISPI_ID0);
    if (dispi_read(VBE_DISPI_INDEX_ID) != VBE_DISPI_ID0) {
        dprintf(1, "No VBE DISPI interface detected\n");
        return -1;
    }

    SET_BDA(vbe_flag, 0x1);
    dispi_write(VBE_DISPI_INDEX_ID, VBE_DISPI_ID5);

    u32 lfb_addr;
    if (CONFIG_VGA_PCI)
        lfb_addr = pci_config_readl(GET_GLOBAL(VgaBDF), 0x10) & ~0xf;
    else
        lfb_addr = VBE_DISPI_LFB_PHYSICAL_ADDRESS;

    SET_FARVAR(get_global_seg(), pci_lfb_addr, lfb_addr);

    dprintf(1, "VBE DISPI detected. lfb_addr=%x\n", GET_GLOBAL(pci_lfb_addr));

    return 0;
}

int
bochsvga_enabled(void)
{
    return GET_BDA(vbe_flag);
}

u16
bochsvga_total_mem(void)
{
    return dispi_read(VBE_DISPI_INDEX_VIDEO_MEMORY_64K);
}

struct vgamode_s *bochsvga_find_mode(int mode)
{
    struct bochsvga_mode *m = bochsvga_modes;
    for (; m < &bochsvga_modes[ARRAY_SIZE(bochsvga_modes)]; m++)
        if (GET_GLOBAL(m->mode) == mode)
            return &m->info;
    return stdvga_find_mode(mode);
}

static int mode_valid(struct vgamode_s *vmode_g)
{
    u16 max_xres = dispi_get_max_xres();
    u16 max_bpp = dispi_get_max_bpp();
    u32 max_mem = bochsvga_total_mem() * 64 * 1024;

    u32 mem = GET_GLOBAL(vmode_g->width) * GET_GLOBAL(vmode_g->height) *
              BYTES_PER_PIXEL(vmode_g);

    if (GET_GLOBAL(vmode_g->width) > max_xres ||
        GET_GLOBAL(vmode_g->depth) > max_bpp ||
        mem > max_mem)
        return 0;

    return 1;
}

int
bochsvga_list_modes(u16 seg, u16 ptr)
{
    int count = 0;
    u16 *dest = (u16 *)(u32)ptr;

    struct bochsvga_mode *m = bochsvga_modes;
    for (; m < &bochsvga_modes[ARRAY_SIZE(bochsvga_modes)]; m++) {
        if (!mode_valid(&m->info))
            continue;

        dprintf(1, "VBE found mode %x valid.\n", GET_GLOBAL(m->mode));
        SET_FARVAR(seg, dest[count], GET_GLOBAL(m->mode));

        count++;
    }

    SET_FARVAR(seg, dest[count], 0xffff); /* End of list */

    return count;
}

int
bochsvga_mode_info(u16 mode, struct vbe_modeinfo *info)
{
    struct vgamode_s *vmode_g = bochsvga_find_mode(mode);
    if (!vmode_g || !mode_valid(vmode_g))
        return -1;

    info->width = GET_GLOBAL(vmode_g->width);
    info->height = GET_GLOBAL(vmode_g->height);
    info->depth = GET_GLOBAL(vmode_g->depth);

    info->linesize = info->width * ((info->depth + 7) / 8);
    info->phys_base = GET_GLOBAL(pci_lfb_addr);
    info->vram_size = bochsvga_total_mem() * 64 * 1024;

    return 0;
}

void
bochsvga_hires_enable(int enable)
{
    u16 flags = enable ?
        VBE_DISPI_ENABLED |
        VBE_DISPI_LFB_ENABLED |
        VBE_DISPI_NOCLEARMEM : 0;

    dispi_write(VBE_DISPI_INDEX_ENABLE, flags);
}

int
bochsvga_set_mode(int mode, int flags)
{
    if (!(mode & VBE_MODE_VESA_DEFINED)) {
        dprintf(1, "set VGA mode %x\n", mode);

        bochsvga_hires_enable(0);
        return stdvga_set_mode(mode, flags);
    }

    struct vbe_modeinfo modeinfo, *info = &modeinfo;
    int ret = bochsvga_mode_info(mode, &modeinfo);
    if (ret) {
        dprintf(1, "VBE mode %x not found\n", mode);
        return VBE_RETURN_STATUS_FAILED;
    }
    bochsvga_hires_enable(1);

    if (info->depth == 4)
        stdvga_set_mode(0x6a, 0);
    if (info->depth == 8)
        // XXX load_dac_palette(3);
        ;

    dispi_write(VBE_DISPI_INDEX_BPP, info->depth);
    dispi_write(VBE_DISPI_INDEX_XRES, info->width);
    dispi_write(VBE_DISPI_INDEX_YRES, info->height);
    dispi_write(VBE_DISPI_INDEX_BANK, 0);

    /* VGA compat setup */
    //XXX: This probably needs some reverse engineering
    u8 v;
    outw(0x0011, VGAREG_VGA_CRTC_ADDRESS);
    outw(((info->width * 4 - 1) << 8) | 0x1, VGAREG_VGA_CRTC_ADDRESS);
    dispi_write(VBE_DISPI_INDEX_VIRT_WIDTH, info->width);
    outw(((info->height - 1) << 8) | 0x12, VGAREG_VGA_CRTC_ADDRESS);
    outw(((info->height - 1) & 0xff00) | 0x7, VGAREG_VGA_CRTC_ADDRESS);
    v = inb(VGAREG_VGA_CRTC_DATA) & 0xbd;
    if (v & 0x1)
        v |= 0x2;
    if (v & 0x2)
        v |= 0x40;
    outb(v, VGAREG_VGA_CRTC_DATA);

    outw(0x9, VGAREG_VGA_CRTC_ADDRESS);
    outb(0x17, VGAREG_VGA_CRTC_ADDRESS);
    outb(inb(VGAREG_VGA_CRTC_DATA) | 0x3, VGAREG_VGA_CRTC_DATA);
    v = inb(VGAREG_ACTL_RESET);
    outw(0x10, VGAREG_ACTL_ADDRESS);
    v = inb(VGAREG_ACTL_READ_DATA) | 0x1;
    outb(v, VGAREG_ACTL_ADDRESS);
    outb(0x20, VGAREG_ACTL_ADDRESS);
    outw(0x0506, VGAREG_GRDC_ADDRESS);
    outw(0x0f02, VGAREG_SEQU_ADDRESS);
    if (info->depth >= 8) {
        outb(0x14, VGAREG_VGA_CRTC_ADDRESS);
        outb(inb(VGAREG_VGA_CRTC_DATA) | 0x40, VGAREG_VGA_CRTC_DATA);
        v = inb(VGAREG_ACTL_RESET);
        outw(0x10, VGAREG_ACTL_ADDRESS);
        v = inb(VGAREG_ACTL_READ_DATA) | 0x40;
        outb(v, VGAREG_ACTL_ADDRESS);
        outb(0x20, VGAREG_ACTL_ADDRESS);
        outb(0x04, VGAREG_SEQU_ADDRESS);
        v = inb(VGAREG_SEQU_DATA) | 0x08;
        outb(v, VGAREG_SEQU_DATA);
        outb(0x05, VGAREG_GRDC_ADDRESS);
        v = inb(VGAREG_GRDC_DATA) & 0x9f;
        outb(v | 0x40, VGAREG_GRDC_DATA);
    }

    SET_BDA(vbe_mode, mode);

    if (flags & MF_LINEARFB) {
        /* Linear frame buffer */
        /* XXX: ??? */
    }
    if (!(mode & MF_NOCLEARMEM)) {
        bochsvga_clear_scr();
    }

    return 0;
}

void
bochsvga_clear_scr(void)
{
    u16 en;

    en = dispi_read(VBE_DISPI_INDEX_ENABLE);
    en &= ~VBE_DISPI_NOCLEARMEM;
    dispi_write(VBE_DISPI_INDEX_ENABLE, en);
}

int
bochsvga_hires_enabled(void)
{
    return dispi_read(VBE_DISPI_INDEX_ENABLE) & VBE_DISPI_ENABLED;
}

u16
bochsvga_curr_mode(void)
{
    return GET_BDA(vbe_mode);
}
