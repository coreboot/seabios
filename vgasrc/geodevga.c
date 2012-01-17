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

/****************************************************************
* Extened CRTC Register functions
****************************************************************/
static void crtce_lock(void)
{
    stdvga_crtc_write(VGAREG_VGA_CRTC_ADDRESS, EXTENDED_REGISTER_LOCK
                      , CRTCE_LOCK);
}

static void crtce_unlock(void)
{
    stdvga_crtc_write(VGAREG_VGA_CRTC_ADDRESS, EXTENDED_REGISTER_LOCK
                      , CRTCE_UNLOCK);
}

static u8 crtce_read(u8 reg)
{
    crtce_unlock();
    u8 val = stdvga_crtc_read(VGAREG_VGA_CRTC_ADDRESS, reg);
    crtce_lock();
    return val;
}

static void crtce_write(u8 reg, u8 val)
{
    crtce_unlock();
    stdvga_crtc_write(VGAREG_VGA_CRTC_ADDRESS, reg, val);
    crtce_lock();
}

/****************************************************************
* Display Controller Functions
****************************************************************/
static u32 dc_read(u16 seg, u32 reg)
{
    u32 val, *dest_far = (void*)reg;
    val = GET_FARVAR(seg,*dest_far);
    return val;
}

static void dc_write(u16 seg, u32 reg, u32 val)
{
    u32 *dest_far = (void*)reg;
    SET_FARVAR(seg,*dest_far,val);
}

static void dc_set(u16 seg, u32 reg, u32 and, u32 or)
{
    u32 val = dc_read(seg,reg);
    val &=and;
    val |=or;
    dc_write(seg,reg,val);
}

static void dc_unlock(u16 seg)
{
    dc_write(seg,DC_UNLOCK,DC_LOCK_UNLOCK);
}

static void dc_lock(u16 seg)
{
    dc_write(seg,DC_UNLOCK,DC_LOCK_LOCK);
}

static u16 dc_map(u16 seg)
{
    u8 reg;

    reg = crtce_read(EXTENDED_MODE_CONTROL);
    reg &= 0xf9;
    switch (seg) {
    case SEG_GRAPH:
        reg |= 0x02;
        break;
    case SEG_MTEXT:
        reg |= 0x04;
        break;
    case SEG_CTEXT:
        reg |= 0x06;
        break;
    default:
        seg=0;
        break;
    }

    crtce_write(EXTENDED_MODE_CONTROL,reg);
    return seg;
}

static void dc_unmap(void)
{
    dc_map(0);
}


/****************************************************************
* Init Functions
****************************************************************/

/* Set up the dc (display controller) portion of the geodelx
*  The dc provides hardware support for VGA graphics
*  for features not accessible from the VGA registers,
*  the dc's pci bar can be mapped to a vga memory segment
*/
static int dc_setup(void)
{
    u32 fb, dc_fb;
    u16 seg;

    dprintf(2, "DC_SETUP\n");

    seg = dc_map(SEG_GRAPH);
    dc_unlock(seg);

    /* zero memory config */
    dc_write(seg,DC_FB_ST_OFFSET,0x0);
    dc_write(seg,DC_CB_ST_OFFSET,0x0);
    dc_write(seg,DC_CURS_ST_OFFSET,0x0);

    /* read fb-bar from pci, then point dc to the fb base */
    dc_fb = dc_read(seg,DC_GLIU0_MEM_OFFSET);
    fb = pci_config_readl(GET_GLOBAL(VgaBDF), PCI_BASE_ADDRESS_0);
    if (fb!=dc_fb) {
        dc_write(seg,DC_GLIU0_MEM_OFFSET,fb);
    }

    dc_set(seg,DC_DISPLAY_CFG,DC_CFG_MSK,DC_GDEN+DC_TRUP);
    dc_set(seg,DC_GENERAL_CFG,0,DC_VGAE);

    dc_lock(seg);
    dc_unmap();

    return 0;
}

/* Setup the vp (video processor) portion of the geodelx
*  Under VGA modes the vp was handled by softvg from inside VSA2.
*  Without a softvg module, access is only available through a pci bar.
*  The High Mem Access virtual register is used to  configure the
*   pci mmio bar from 16bit friendly io space.
*/
int vp_setup(void)
{
    u32 reg,vp;

    dprintf(2,"VP_SETUP\n");
    /* set output to crt and RGB/YUV */
    if (CONFIG_VGA_GEODEGX2)
        geode_msrWrite(VP_MSR_CONFIG_GX2, ~0, ~0xf8, 0, 0);
    else
        geode_msrWrite(VP_MSR_CONFIG_LX, ~0, ~0xf8, 0, 0);

    /* get vp register base from pci */
    vp = pci_config_readl(GET_GLOBAL(VgaBDF), PCI_BASE_ADDRESS_3);

    /* Set mmio registers
    * there may be some timing issues here, the reads seem
    * to slow things down enough work reliably
    */

    reg = geode_memRead(vp+VP_MISC);
    dprintf(1,"VP_SETUP VP_MISC=0x%08x\n",reg);
    geode_memWrite(vp+VP_MISC,0,VP_BYP_BOTH);
    reg = geode_memRead(vp+VP_MISC);
    dprintf(1,"VP_SETUP VP_MISC=0x%08x\n",reg);

    reg = geode_memRead(vp+VP_DCFG);
    dprintf(1,"VP_SETUP VP_DCFG=0x%08x\n",reg);
    geode_memWrite(vp+VP_DCFG, ~0,VP_CRT_EN+VP_HSYNC_EN+VP_VSYNC_EN+VP_DAC_BL_EN+VP_CRT_SKEW);
    reg = geode_memRead(vp+VP_DCFG);
    dprintf(1,"VP_SETUP VP_DCFG=0x%08x\n",reg);

    return 0;
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
    ret |= vp_setup();
    ret |= dc_setup();

    return ret;
}
