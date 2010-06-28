// Option rom scanning code.
//
// Copyright (C) 2009-2010  coresystems GmbH
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "bregs.h" // struct bregs
#include "farptr.h" // FLATPTR_TO_SEG
#include "config.h" // CONFIG_*
#include "util.h" // dprintf
#include "jpeg.h" // splash

/****************************************************************
 * Definitions
 ****************************************************************/

/****************************************************************
 * VESA structures
 ****************************************************************/

struct vesa_info
{
    u8 vesa_signature[4];
    u16 vesa_version;
    u32 oem_string_ptr;
    u8 capabilities[4];
    u32 video_mode_ptr;
    u16 total_memory;
    u16 oem_software_rev;
    u32 oem_vendor_name_ptr;
    u32 oem_product_name_ptr;
    u32 oem_product_rev_ptr;
    u8 reserved[222];
    u8 oem_data[256];
} PACKED;

struct vesa_mode_info
{
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
    u32 phys_base_ptr;
    u32 offscreen_mem_offset;
    u16 offscreen_mem_size;
    u8 reserved[206];
} PACKED;

/****************************************************************
 * Helper functions
 ****************************************************************/

/****************************************************************
 * VGA text / graphics console
 ****************************************************************/
static void enable_vga_text_console(void)
{
    dprintf(1, "Turning on vga text mode console\n");
    struct bregs br;

    /* Enable VGA text mode */
    memset(&br, 0, sizeof(br));
    br.flags = F_IF;
    br.ax = 0x0003;
    start_preempt();
    call16_int(0x10, &br);
    finish_preempt();

    if (CONFIG_BOOTSPLASH) {
        /* Switch back to start of the framebuffer
         * (disable "double buffering")
         */
        memset(&br, 0, sizeof(br));
        br.flags = F_IF;
        br.ax = 0x4f07;
        br.bl = 0x02;
        br.ecx = 0;

        start_preempt();
        call16_int(0x10, &br);
        finish_preempt();
    }

    // Write to screen.
    printf("Starting SeaBIOS (version %s)\n\n", VERSION);
}

void enable_vga_console(void)
{
    /* Needs coreboot support for CBFS */
    if (!CONFIG_BOOTSPLASH || !CONFIG_COREBOOT) {
        enable_vga_text_console();
        return;
    }

    struct bregs br;
    struct vesa_info *vesa_info;
    struct vesa_mode_info *mode_info;
    struct jpeg_decdata *decdata;

    vesa_info = malloc_tmphigh(sizeof(*vesa_info));
    mode_info = malloc_tmphigh(sizeof(*mode_info));
    decdata = malloc_tmphigh(sizeof(*decdata));

    /* Check whether we have a VESA 2.0 compliant BIOS */
    memset(vesa_info, 0, sizeof(struct vesa_info));
    memcpy(vesa_info, "VBE2", 4);

    memset(&br, 0, sizeof(br));
    br.flags = F_IF;
    br.ax = 0x4f00;
    br.di = FLATPTR_TO_OFFSET(vesa_info);
    br.es = FLATPTR_TO_SEG(vesa_info);
    start_preempt();
    call16_int(0x10, &br);
    finish_preempt();

    if(strcmp("VESA", (char *)vesa_info) != 0) {
        dprintf(1,"No VBE2 found.\n");
        goto cleanup;
    }

    /* Print some debugging information about our card. */
    char *vendor, *product;
    vendor = (char *)(((vesa_info->oem_vendor_name_ptr & 0xffff0000) >> 12) |
                    (vesa_info->oem_vendor_name_ptr & 0xffff));

    product = (char *)(((vesa_info->oem_product_name_ptr & 0xffff0000) >> 12) |
                    (vesa_info->oem_product_name_ptr & 0xffff));

    dprintf(8, "VESA %d.%d\nVENDOR: %s\nPRODUCT: %s\n",
                    vesa_info->vesa_version>>8,vesa_info->vesa_version&0xff,
                    vendor, product);

    /* Get information about our graphics mode, like the
     * framebuffer start address
     */
    memset(&br, 0, sizeof(br));
    br.flags = F_IF;
    br.ax = 0x4f01;
    br.cx = (1 << 14) | CONFIG_BOOTSPLASH_VESA_MODE;
    br.di = FLATPTR_TO_OFFSET(mode_info);
    br.es = FLATPTR_TO_SEG(mode_info);
    start_preempt();
    call16_int(0x10, &br);
    finish_preempt();
    if (br.ax != 0x4f) {
        dprintf(1, "get_mode failed.\n");
        enable_vga_text_console();
        goto cleanup;
    }
    unsigned char *framebuffer = (unsigned char *) (mode_info->phys_base_ptr);

    /* Switch to graphics mode */
    memset(&br, 0, sizeof(br));
    br.flags = F_IF;
    br.ax = 0x4f02;
    br.bx = (1 << 14) | CONFIG_BOOTSPLASH_VESA_MODE;
    start_preempt();
    call16_int(0x10, &br);
    finish_preempt();
    if (br.ax != 0x4f) {
        dprintf(1, "set_mode failed.\n");
        enable_vga_text_console();
        goto cleanup;
    }

    /* Switching Intel IGD to 1MB video memory will break this. Who cares. */
    int imagesize = CONFIG_BOOTSPLASH_X * CONFIG_BOOTSPLASH_Y *
                        (CONFIG_BOOTSPLASH_DEPTH / 8);

    /* We use "double buffering" to make things look nicer */
    framebuffer += imagesize;

    dprintf(9, "framebuffer: %x\n", (u32)framebuffer);
    dprintf(9, "bytes per scanline: %d\n", mode_info->bytes_per_scanline);
    dprintf(9, "bits per pixel: %d\n", mode_info->bits_per_pixel);

    /* Look for bootsplash.jpg in CBFS and decompress it... */
    int ret = 0;
    unsigned char *jpeg = NULL;

    struct cbfs_file *file = cbfs_finddatafile("bootsplash.jpg");
    int filesize = 0;

    if (file) {
        filesize = cbfs_datasize(file);
        jpeg = malloc_tmphigh(filesize);
    } else {
        dprintf(1, "Could not find boot splash screen \"bootsplash.jpg\"\n");
    }
    if(jpeg) {
        dprintf(9, "Copying boot splash screen...\n");
        cbfs_copyfile(file, jpeg, filesize);
        dprintf(9, "Decompressing boot splash screen...\n");
        start_preempt();
        ret = jpeg_decode(jpeg, framebuffer, CONFIG_BOOTSPLASH_X,
                         CONFIG_BOOTSPLASH_Y, CONFIG_BOOTSPLASH_DEPTH, decdata);
        finish_preempt();
        if (ret)
            dprintf(1, "Failed with return code %x...\n", ret);
    } else {
        ret = -1;
    }
    free(jpeg);
    if (ret) {
        enable_vga_text_console();
        goto cleanup;
    }

    /* Show the picture */
    memset(&br, 0, sizeof(br));
    br.flags = F_IF;
    br.ax = 0x4f07;
    br.bl = 0x02;
    br.ecx = imagesize;
    start_preempt();
    call16_int(0x10, &br);
    finish_preempt();
    if (br.ax != 0x4f) {
        dprintf(1, "display_start failed.\n");
        enable_vga_text_console();
    }

cleanup:
    free (vesa_info);
    free (mode_info);
    free (decdata);
}

void
disable_bootsplash(void)
{
    if (! CONFIG_BOOTSPLASH)
        return;
    enable_vga_text_console();
}
