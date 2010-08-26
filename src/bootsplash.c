// Option rom scanning code.
//
// Copyright (C) 2009-2010  coresystems GmbH
// Copyright (C) 2010  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "bregs.h" // struct bregs
#include "farptr.h" // FLATPTR_TO_SEG
#include "config.h" // CONFIG_*
#include "util.h" // dprintf
#include "jpeg.h" // splash
#include "biosvar.h" // SET_EBDA
#include "paravirt.h" // romfile_find


/****************************************************************
 * VESA structures
 ****************************************************************/

struct vesa_info {
    u32 vesa_signature;
    u16 vesa_version;
    struct segoff_s oem_string_ptr;
    u8 capabilities[4];
    struct segoff_s video_mode_ptr;
    u16 total_memory;
    u16 oem_software_rev;
    struct segoff_s oem_vendor_name_ptr;
    struct segoff_s oem_product_name_ptr;
    struct segoff_s oem_product_rev_ptr;
    u8 reserved[222];
    u8 oem_data[256];
} PACKED;

#define VESA_SIGNATURE 0x41534556 // VESA
#define VBE2_SIGNATURE 0x32454256 // VBE2

struct vesa_mode_info {
    u16 mode_attributes;
    u8 win_a_attributes;
    u8 win_b_attributes;
    u16 win_granularity;
    u16 win_size;
    u16 win_a_segment;
    u16 win_b_segment;
    u32 win_func_ptr;
    u16 bytes_per_scanline;
    u16 x_resolution;
    u16 y_resolution;
    u8 x_charsize;
    u8 y_charsize;
    u8 number_of_planes;
    u8 bits_per_pixel;
    u8 number_of_banks;
    u8 memory_model;
    u8 bank_size;
    u8 number_of_image_pages;
    u8 reserved_page;
    u8 red_mask_size;
    u8 red_mask_pos;
    u8 green_mask_size;
    u8 green_mask_pos;
    u8 blue_mask_size;
    u8 blue_mask_pos;
    u8 reserved_mask_size;
    u8 reserved_mask_pos;
    u8 direct_color_mode_info;
    void *phys_base_ptr;
    u32 offscreen_mem_offset;
    u16 offscreen_mem_size;
    u8 reserved[206];
} PACKED;


/****************************************************************
 * Helper functions
 ****************************************************************/

// Call int10 vga handler.
static void
call16_int10(struct bregs *br)
{
    br->flags = F_IF;
    start_preempt();
    call16_int(0x10, br);
    finish_preempt();
}


/****************************************************************
 * VGA text / graphics console
 ****************************************************************/

void
enable_vga_console(void)
{
    dprintf(1, "Turning on vga text mode console\n");
    struct bregs br;

    /* Enable VGA text mode */
    memset(&br, 0, sizeof(br));
    br.ax = 0x0003;
    call16_int10(&br);

    // Write to screen.
    printf("SeaBIOS (version %s)\n\n", VERSION);
}

static int
find_videomode(struct vesa_info *vesa_info, struct vesa_mode_info *mode_info
               , int width, int height)
{
    dprintf(3, "Finding vesa mode with dimensions %d/%d\n", width, height);
    u16 *videomodes = SEGOFF_TO_FLATPTR(vesa_info->video_mode_ptr);
    for (;; videomodes++) {
        u16 videomode = *videomodes;
        if (videomode == 0xffff) {
            dprintf(1, "Unable to find vesa video mode dimensions %d/%d\n"
                    , width, height);
            return -1;
        }
        struct bregs br;
        memset(&br, 0, sizeof(br));
        br.ax = 0x4f01;
        br.cx = (1 << 14) | videomode;
        br.di = FLATPTR_TO_OFFSET(mode_info);
        br.es = FLATPTR_TO_SEG(mode_info);
        call16_int10(&br);
        if (br.ax != 0x4f) {
            dprintf(1, "get_mode failed.\n");
            continue;
        }
        if (mode_info->x_resolution != width
            || mode_info->y_resolution != height)
            continue;
        u8 depth = mode_info->bits_per_pixel;
        if (depth != 16 && depth != 24 && depth != 32)
            continue;
        return videomode;
    }
}

static int BootsplashActive;

void
enable_bootsplash(void)
{
    if (!CONFIG_BOOTSPLASH)
        return;
    dprintf(3, "Checking for bootsplash\n");
    u32 file = romfile_find("bootsplash.jpg");
    if (!file)
        return;
    int filesize = romfile_size(file);

    u8 *picture = NULL;
    u8 *filedata = malloc_tmphigh(filesize);
    struct vesa_info *vesa_info = malloc_tmplow(sizeof(*vesa_info));
    struct vesa_mode_info *mode_info = malloc_tmplow(sizeof(*mode_info));
    struct jpeg_decdata *jpeg = jpeg_alloc();
    if (!filedata || !jpeg || !vesa_info || !mode_info) {
        warn_noalloc();
        goto done;
    }

    /* Check whether we have a VESA 2.0 compliant BIOS */
    memset(vesa_info, 0, sizeof(struct vesa_info));
    vesa_info->vesa_signature = VBE2_SIGNATURE;
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.ax = 0x4f00;
    br.di = FLATPTR_TO_OFFSET(vesa_info);
    br.es = FLATPTR_TO_SEG(vesa_info);
    call16_int10(&br);
    if (vesa_info->vesa_signature != VESA_SIGNATURE) {
        dprintf(1,"No VBE2 found.\n");
        goto done;
    }

    /* Print some debugging information about our card. */
    char *vendor = SEGOFF_TO_FLATPTR(vesa_info->oem_vendor_name_ptr);
    char *product = SEGOFF_TO_FLATPTR(vesa_info->oem_product_name_ptr);
    dprintf(3, "VESA %d.%d\nVENDOR: %s\nPRODUCT: %s\n",
            vesa_info->vesa_version>>8, vesa_info->vesa_version&0xff,
            vendor, product);

    // Parse jpeg and get image size.
    dprintf(5, "Copying bootsplash.jpg\n");
    romfile_copy(file, filedata, filesize);
    dprintf(5, "Decoding bootsplash.jpg\n");
    int ret = jpeg_decode(jpeg, filedata);
    if (ret) {
        dprintf(1, "jpeg_decode failed with return code %d...\n", ret);
        goto done;
    }
    int width, height;
    jpeg_get_size(jpeg, &width, &height);

    // Try to find a graphics mode with the corresponding dimensions.
    int videomode = find_videomode(vesa_info, mode_info, width, height);
    if (videomode < 0)
        goto done;
    void *framebuffer = mode_info->phys_base_ptr;
    int depth = mode_info->bits_per_pixel;
    dprintf(3, "mode: %04x\n", videomode);
    dprintf(3, "framebuffer: %p\n", framebuffer);
    dprintf(3, "bytes per scanline: %d\n", mode_info->bytes_per_scanline);
    dprintf(3, "bits per pixel: %d\n", depth);

    // Allocate space for image and decompress it.
    int imagesize = width * height * (depth / 8);
    picture = malloc_tmphigh(imagesize);
    if (!picture) {
        warn_noalloc();
        goto done;
    }
    dprintf(5, "Decompressing bootsplash.jpg\n");
    ret = jpeg_show(jpeg, picture, width, height, depth);
    if (ret) {
        dprintf(1, "jpeg_show failed with return code %d...\n", ret);
        goto done;
    }

    /* Switch to graphics mode */
    dprintf(5, "Switching to graphics mode\n");
    memset(&br, 0, sizeof(br));
    br.ax = 0x4f02;
    br.bx = (1 << 14) | videomode;
    call16_int10(&br);
    if (br.ax != 0x4f) {
        dprintf(1, "set_mode failed.\n");
        goto done;
    }

    /* Show the picture */
    dprintf(5, "Showing bootsplash.jpg\n");
    iomemcpy(framebuffer, picture, imagesize);
    dprintf(5, "Bootsplash copy complete\n");
    BootsplashActive = 1;

done:
    free(filedata);
    free(picture);
    free(vesa_info);
    free(mode_info);
    free(jpeg);
    return;
}

void
disable_bootsplash(void)
{
    if (!CONFIG_BOOTSPLASH || !BootsplashActive)
        return;
    BootsplashActive = 0;
    enable_vga_console();
}
