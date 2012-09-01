// Geode GX2/LX VGA functions
//
// Copyright (C) 2009 Chris Kindt
//
// Written for Google Summer of Code 2009 for the coreboot project
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "geodevga.h" // geodevga_init
#include "farptr.h" // SET_FARVAR
#include "biosvar.h" // GET_BDA
#include "vgabios.h" // VGAREG_*
#include "util.h" // memset
#include "stdvga.h" // stdvga_crtc_write
#include "pci.h" // pci_config_readl
#include "pci_regs.h" // PCI_BASE_ADDRESS_0


/****************************************************************
* MSR and High Mem access through VSA Virtual Register
****************************************************************/

static union u64_u32_u geode_msrRead(u32 msrAddr)
{
    union u64_u32_u val;
    asm __volatile__ (
        "movw   $0x0AC1C, %%dx          \n"
        "movl   $0xFC530007, %%eax      \n"
        "outl   %%eax, %%dx             \n"
        "addb   $2, %%dl                \n"
        "inw    %%dx, %%ax              \n"
        : "=a" (val.lo), "=d"(val.hi)
        : "c"(msrAddr)
        : "cc"
    );
    return val;
}

static void geode_msrWrite(u32 msrAddr,u32 andhi, u32 andlo, u32 orhi, u32 orlo)
{
    asm __volatile__ (
        "push   %%eax                   \n"
        "movw   $0x0AC1C, %%dx          \n"
        "movl   $0xFC530007, %%eax      \n"
        "outl   %%eax, %%dx             \n"
        "addb   $2, %%dl                \n"
        "pop    %%eax                   \n"
        "outw   %%ax, %%dx              \n"
        :
        : "c"(msrAddr), "S" (andhi), "D" (andlo), "b" (orhi), "a" (orlo)
        : "%edx","cc"
    );
}

static u32 geode_memRead(u32 addr)
{
    u32 val;
    asm __volatile__ (
        "movw   $0x0AC1C, %%dx          \n"
        "movl   $0xFC530001, %%eax      \n"
        "outl   %%eax, %%dx             \n"
        "addb   $2, %%dl                \n"
        "inw    %%dx, %%ax              \n"
        : "=a" (val)
        : "b"(addr)
        : "cc"
    );

    return val;
}

static void geode_memWrite(u32 addr, u32 and, u32 or )
{
    asm __volatile__ (
        "movw   $0x0AC1C, %%dx          \n"
        "movl   $0xFC530001, %%eax      \n"
        "outl   %%eax, %%dx             \n"
        "addb   $2, %%dl                \n"
        "outw   %%ax, %%dx              \n"
        :
        : "b"(addr), "S" (and), "D" (or)
        : "%eax","cc"
    );
}

/****************************************************************
 * Helper functions
 ****************************************************************/

struct geode {
    u32 fb;
    u32 dc;
    u32 vp;
};
struct geode geode VAR16;

static int legacyio_check(void)
{
    int ret=0;
    union u64_u32_u val;

    if (CONFIG_VGA_GEODEGX2)
        val=geode_msrRead(GLIU0_P2D_BM_4);
    else
        val=geode_msrRead(MSR_GLIU0_BASE4);
    if (val.lo != 0x0A0fffe0)
        ret|=1;

    val=geode_msrRead(GLIU0_IOD_BM_0);
    if (val.lo != 0x3c0ffff0)
        ret|=2;

    val=geode_msrRead(GLIU0_IOD_BM_1);
    if (val.lo != 0x3d0ffff0)
        ret|=4;

    return ret;
}

static u32 framebuffer_size(void)
{
    u32 val;
    union u64_u32_u msr;

    /* We use the P2D_R0 msr to read out the number of pages.
     * One page has a size of 4k
     *
     * Bit      Name    Description
     * 39:20    PMAX    Physical Memory Address Max
     * 19:0     PMIX    Physical Memory Address Min
     *
     */
    msr = geode_msrRead(GLIU0_P2D_RO);

    u32 pmax = ((msr.hi & 0xff) << 12) | ((msr.lo & 0xfff00000) >> 20);
    u32 pmin = (msr.lo & 0x000fffff);
    
    val = pmax - pmin;
    val += 1;

    /* The page size is 4k */
    return (val << 12);
}

/****************************************************************
* Init Functions
****************************************************************/

/* Set up the dc (display controller) portion of the geodelx
*  The dc provides hardware support for VGA graphics.
*/
static void dc_setup(void)
{
    u32 dc_fb;

    dprintf(2, "DC_SETUP\n");

    geode_memWrite(geode.dc + DC_UNLOCK, 0x0, DC_LOCK_UNLOCK);

    /* zero memory config */
    geode_memWrite(geode.dc + DC_FB_ST_OFFSET, 0x0, 0x0);
    geode_memWrite(geode.dc + DC_CB_ST_OFFSET, 0x0, 0x0);
    geode_memWrite(geode.dc + DC_CURS_ST_OFFSET, 0x0, 0x0);

    /* read fb-bar from pci, then point dc to the fb base */
    dc_fb = geode_memRead(geode.dc + DC_GLIU0_MEM_OFFSET);
    if (geode.fb != dc_fb) {
        geode_memWrite(geode.dc + DC_GLIU0_MEM_OFFSET, 0x0, geode.fb);
    }

    geode_memWrite(geode.dc + DC_DISPLAY_CFG, DC_CFG_MSK, DC_GDEN+DC_TRUP);
    geode_memWrite(geode.dc + DC_GENERAL_CFG, 0, DC_VGAE);

    geode_memWrite(geode.dc + DC_UNLOCK, 0x0, DC_LOCK_LOCK);

    u32 fb_size = framebuffer_size(); // in byte
    dprintf(1, "%d KB of video memory at 0x%08x\n", fb_size / 1024, geode.fb);

    /* update VBE variables */
    SET_VGA(VBE_framebuffer, geode.fb);
    SET_VGA(VBE_total_memory, fb_size / 1024 / 64); // number of 64K blocks
}

/* Setup the vp (video processor) portion of the geodelx
*  Under VGA modes the vp was handled by softvg from inside VSA2.
*  Without a softvg module, access is only available through a pci bar.
*  The High Mem Access virtual register is used to  configure the
*   pci mmio bar from 16bit friendly io space.
*/
static void vp_setup(void)
{
    u32 reg;

    dprintf(2,"VP_SETUP\n");
    /* set output to crt and RGB/YUV */
    if (CONFIG_VGA_GEODEGX2)
        geode_msrWrite(VP_MSR_CONFIG_GX2, ~0, ~0xf8, 0, 0);
    else
        geode_msrWrite(VP_MSR_CONFIG_LX, ~0, ~0xf8, 0, 0);

    /* Set mmio registers
    * there may be some timing issues here, the reads seem
    * to slow things down enough work reliably
    */

    reg = geode_memRead(geode.vp + VP_MISC);
    dprintf(1,"VP_SETUP VP_MISC=0x%08x\n",reg);
    geode_memWrite(geode.vp + VP_MISC,0,VP_BYP_BOTH);
    reg = geode_memRead(geode.vp + VP_MISC);
    dprintf(1,"VP_SETUP VP_MISC=0x%08x\n",reg);

    reg = geode_memRead(geode.vp + VP_DCFG);
    dprintf(1,"VP_SETUP VP_DCFG=0x%08x\n",reg);
    geode_memWrite(geode.vp + VP_DCFG, ~0,VP_CRT_EN+VP_HSYNC_EN+VP_VSYNC_EN+VP_DAC_BL_EN+VP_CRT_SKEW);
    reg = geode_memRead(geode.vp + VP_DCFG);
    dprintf(1,"VP_SETUP VP_DCFG=0x%08x\n",reg);
}

static u8 geode_crtc_01[] VAR16 = {
    0x2d, 0x27, 0x28, 0x90, 0x29, 0x8e, 0xbf, 0x1f,
    0x00, 0x4f, 0x0d, 0x0e, 0x00, 0x00, 0x00, 0x00,
    0x9b, 0x8d, 0x8f, 0x14, 0x1f, 0x97, 0xb9, 0xa3,
    0xff };
static u8 geode_crtc_03[] VAR16 = {
    0x5f, 0x4f, 0x50, 0x82, 0x51, 0x9e, 0xbf, 0x1f,
    0x00, 0x4f, 0x0d, 0x0e, 0x00, 0x00, 0x00, 0x00,
    0x9b, 0x8d, 0x8f, 0x28, 0x1f, 0x97, 0xb9, 0xa3,
    0xff };
static u8 geode_crtc_04[] VAR16 = {
    0x2d, 0x27, 0x28, 0x90, 0x29, 0x8e, 0xbf, 0x1f,
    0x00, 0xc1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9b, 0x8d, 0x8f, 0x14, 0x00, 0x97, 0xb9, 0xa2,
    0xff };
static u8 geode_crtc_05[] VAR16 = {
    0x2d, 0x27, 0x28, 0x90, 0x29, 0x8e, 0xbf, 0x1f,
    0x00, 0xc1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9b, 0x8e, 0x8f, 0x14, 0x00, 0x97, 0xb9, 0xa2,
    0xff };
static u8 geode_crtc_06[] VAR16 = {
    0x5f, 0x4f, 0x50, 0x82, 0x51, 0x9e, 0xbf, 0x1f,
    0x00, 0xc1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9b, 0x8d, 0x8f, 0x28, 0x00, 0x97, 0xb9, 0xc2,
    0xff };
static u8 geode_crtc_07[] VAR16 = {
    0x5f, 0x4f, 0x50, 0x82, 0x51, 0x9e, 0xbf, 0x1f,
    0x00, 0x4f, 0x0d, 0x0e, 0x00, 0x00, 0x00, 0x00,
    0x9b, 0x8d, 0x8f, 0x28, 0x0f, 0x97, 0xb9, 0xa3,
    0xff };
static u8 geode_crtc_0d[] VAR16 = {
    0x2d, 0x27, 0x28, 0x90, 0x29, 0x8e, 0xbf, 0x1f,
    0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9b, 0x8d, 0x8f, 0x14, 0x00, 0x97, 0xb9, 0xe3,
    0xff };
static u8 geode_crtc_0e[] VAR16 = {
    0x5f, 0x4f, 0x50, 0x82, 0x51, 0x9e, 0xbf, 0x1f,
    0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9b, 0x8d, 0x8f, 0x28, 0x00, 0x97, 0xb9, 0xe3,
    0xff };
static u8 geode_crtc_0f[] VAR16 = {
    0x5f, 0x4f, 0x50, 0x82, 0x51, 0x9e, 0xbf, 0x1f,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x83, 0x85, 0x5d, 0x28, 0x0f, 0x65, 0xb9, 0xe3,
    0xff };
static u8 geode_crtc_11[] VAR16 = {
    0x5f, 0x4f, 0x50, 0x82, 0x51, 0x9e, 0x0b, 0x3e,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xe9, 0x8b, 0xdf, 0x28, 0x00, 0xe7, 0x04, 0xe3,
    0xff };
static u8 geode_crtc_13[] VAR16 = {
    0x5f, 0x4f, 0x50, 0x82, 0x51, 0x9e, 0xbf, 0x1f,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9b, 0x8d, 0x8f, 0x28, 0x40, 0x98, 0xb9, 0xa3,
    0xff };

int geodevga_init(void)
{
    int ret = stdvga_init();
    if (ret)
        return ret;

    dprintf(1,"GEODEVGA_INIT\n");

    if ((ret=legacyio_check())) {
        dprintf(1,"GEODEVGA_INIT legacyio_check=0x%x\n",ret);
    }

    // Updated timings from geode datasheets, table 6-53 in particular
    static u8 *new_crtc[] VAR16 = {
        geode_crtc_01, geode_crtc_01, geode_crtc_03, geode_crtc_03,
        geode_crtc_04, geode_crtc_05, geode_crtc_06, geode_crtc_07,
        0, 0, 0, 0, 0,
        geode_crtc_0d, geode_crtc_0e, geode_crtc_0f, geode_crtc_0f,
        geode_crtc_11, geode_crtc_11, geode_crtc_13 };
    int i;
    for (i=0; i<ARRAY_SIZE(new_crtc); i++) {
        u8 *crtc = GET_GLOBAL(new_crtc[i]);
        if (crtc)
            stdvga_override_crtc(i, crtc);
    }

    if (GET_GLOBAL(VgaBDF) < 0)
        // Device should be at 00:01.1
        SET_VGA(VgaBDF, pci_to_bdf(0, 1, 1));
    
    // setup geode struct which is used for register access
    geode.fb = pci_config_readl(GET_GLOBAL(VgaBDF), PCI_BASE_ADDRESS_0);
    geode.dc = pci_config_readl(GET_GLOBAL(VgaBDF), PCI_BASE_ADDRESS_2);
    geode.vp = pci_config_readl(GET_GLOBAL(VgaBDF), PCI_BASE_ADDRESS_3);
    
    dprintf(1, "fb addr: 0x%08x\n", geode.fb);
    dprintf(1, "dc addr: 0x%08x\n", geode.dc);
    dprintf(1, "vp addr: 0x%08x\n", geode.vp);
    
    vp_setup();
    dc_setup();

    return 0;
}
