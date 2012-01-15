#include "vgabios.h" // struct vbe_modeinfo
#include "vbe.h" // VBE_MODE_VESA_DEFINED
#include "bochsvga.h" // bochsvga_set_mode
#include "util.h" // dprintf
#include "config.h" // CONFIG_*
#include "biosvar.h" // SET_BDA
#include "stdvga.h" // VGAREG_SEQU_ADDRESS
#include "pci.h" // pci_config_readl
#include "pci_regs.h" // PCI_BASE_ADDRESS_0

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

    dispi_write(VBE_DISPI_INDEX_ID, VBE_DISPI_ID5);

    u32 lfb_addr;
    if (CONFIG_VGA_PCI)
        lfb_addr = (pci_config_readl(GET_GLOBAL(VgaBDF), PCI_BASE_ADDRESS_0)
                    & PCI_BASE_ADDRESS_MEM_MASK);
    else
        lfb_addr = VBE_DISPI_LFB_PHYSICAL_ADDRESS;

    SET_VGA(VBE_framebuffer, lfb_addr);
    u16 totalmem = dispi_read(VBE_DISPI_INDEX_VIDEO_MEMORY_64K);
    SET_VGA(VBE_total_memory, totalmem * 64 * 1024);
    SET_VGA(VBE_capabilities, VBE_CAPABILITY_8BIT_DAC);

    dprintf(1, "VBE DISPI detected. lfb_addr=%x\n", lfb_addr);

    return 0;
}

static int mode_valid(struct vgamode_s *vmode_g)
{
    u16 max_xres = dispi_get_max_xres();
    u16 max_bpp = dispi_get_max_bpp();
    u32 max_mem = GET_GLOBAL(VBE_total_memory);

    u16 width = GET_GLOBAL(vmode_g->width);
    u16 height = GET_GLOBAL(vmode_g->height);
    u8 depth = GET_GLOBAL(vmode_g->depth);
    u32 mem = width * height * DIV_ROUND_UP(depth, 8);

    return width <= max_xres && depth <= max_bpp && mem <= max_mem;
}

struct vgamode_s *bochsvga_find_mode(int mode)
{
    struct bochsvga_mode *m = bochsvga_modes;
    for (; m < &bochsvga_modes[ARRAY_SIZE(bochsvga_modes)]; m++)
        if (GET_GLOBAL(m->mode) == mode) {
            if (! mode_valid(&m->info))
                return NULL;
            return &m->info;
        }
    return stdvga_find_mode(mode);
}

void
bochsvga_list_modes(u16 seg, u16 *dest, u16 *last)
{
    struct bochsvga_mode *m = bochsvga_modes;
    for (; m < &bochsvga_modes[ARRAY_SIZE(bochsvga_modes)] && dest<last; m++) {
        if (!mode_valid(&m->info))
            continue;

        dprintf(1, "VBE found mode %x valid.\n", GET_GLOBAL(m->mode));
        SET_FARVAR(seg, *dest, GET_GLOBAL(m->mode));
        dest++;
    }

    stdvga_list_modes(seg, dest, last);
}

static void
bochsvga_hires_enable(int enable)
{
    u16 flags = enable ?
        VBE_DISPI_ENABLED |
        VBE_DISPI_LFB_ENABLED |
        VBE_DISPI_NOCLEARMEM : 0;

    dispi_write(VBE_DISPI_INDEX_ENABLE, flags);
}

static void
bochsvga_clear_scr(void)
{
    u16 en;

    en = dispi_read(VBE_DISPI_INDEX_ENABLE);
    en &= ~VBE_DISPI_NOCLEARMEM;
    dispi_write(VBE_DISPI_INDEX_ENABLE, en);
}

int
bochsvga_set_mode(int mode, int flags)
{
    if (!(mode & VBE_MODE_VESA_DEFINED)) {
        dprintf(1, "set VGA mode %x\n", mode);

        SET_BDA(vbe_mode, 0);
        bochsvga_hires_enable(0);
        return stdvga_set_mode(mode, flags);
    }

    struct vgamode_s *vmode_g = bochsvga_find_mode(mode);
    if (!vmode_g) {
        dprintf(1, "VBE mode %x not found\n", mode);
        return VBE_RETURN_STATUS_FAILED;
    }
    bochsvga_hires_enable(1);

    u8 depth = GET_GLOBAL(vmode_g->depth);
    if (depth == 4)
        stdvga_set_mode(0x6a, 0);
    if (depth == 8)
        // XXX load_dac_palette(3);
        ;

    dispi_write(VBE_DISPI_INDEX_BPP, depth);
    u16 width = GET_GLOBAL(vmode_g->width);
    u16 height = GET_GLOBAL(vmode_g->height);
    dispi_write(VBE_DISPI_INDEX_XRES, width);
    dispi_write(VBE_DISPI_INDEX_YRES, height);
    dispi_write(VBE_DISPI_INDEX_BANK, 0);

    /* VGA compat setup */
    //XXX: This probably needs some reverse engineering
    u16 crtc_addr = VGAREG_VGA_CRTC_ADDRESS;
    stdvga_crtc_write(crtc_addr, 0x11, 0x00);
    stdvga_crtc_write(crtc_addr, 0x01, width / 8 - 1);
    dispi_write(VBE_DISPI_INDEX_VIRT_WIDTH, width);
    stdvga_crtc_write(crtc_addr, 0x12, height - 1);
    u8 v = 0;
    if ((height - 1) & 0x0100)
        v |= 0x02;
    if ((height - 1) & 0x0200)
        v |= 0x40;
    stdvga_crtc_mask(crtc_addr, 0x07, 0x42, v);

    stdvga_crtc_write(crtc_addr, 0x09, 0x00);
    stdvga_crtc_mask(crtc_addr, 0x17, 0x00, 0x03);
    stdvga_attr_mask(0x10, 0x00, 0x01);
    stdvga_grdc_write(0x06, 0x05);
    stdvga_sequ_write(0x02, 0x0f);
    if (depth >= 8) {
        stdvga_crtc_mask(crtc_addr, 0x14, 0x00, 0x40);
        stdvga_attr_mask(0x10, 0x00, 0x40);
        stdvga_sequ_mask(0x04, 0x00, 0x08);
        stdvga_grdc_mask(0x05, 0x20, 0x40);
    }

    SET_BDA(vbe_mode, mode | flags);

    if (flags & MF_LINEARFB) {
        /* Linear frame buffer */
        /* XXX: ??? */
    }
    if (!(mode & MF_NOCLEARMEM)) {
        bochsvga_clear_scr();
    }

    return 0;
}
