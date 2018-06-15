#include "biosvar.h" // GET_BDA
#include "output.h" // dprintf
#include "string.h" // memset16_far
#include "bochsvga.h" // VBE_BOCHS_*
#include "hw/pci.h" // pci_config_readl
#include "hw/pci_regs.h" // PCI_BASE_ADDRESS_0
#include "vgautil.h" // VBE_total_memory

#define FRAMEBUFFER_WIDTH      1024
#define FRAMEBUFFER_HEIGHT     768
#define FRAMEBUFFER_BPP        4
#define FRAMEBUFFER_STRIDE     (FRAMEBUFFER_BPP * FRAMEBUFFER_WIDTH)
#define FRAMEBUFFER_SIZE       (FRAMEBUFFER_STRIDE * FRAMEBUFFER_HEIGHT)

int
bochs_display_setup(void)
{
    dprintf(1, "bochs-display: setup called\n");

    if (GET_GLOBAL(HaveRunInit))
        return 0;

    int bdf = GET_GLOBAL(VgaBDF);
    if (bdf == 0)
        return 0;

    u32 bar = pci_config_readl(bdf, PCI_BASE_ADDRESS_0);
    u32 lfb_addr = bar & PCI_BASE_ADDRESS_MEM_MASK;
    bar = pci_config_readl(bdf, PCI_BASE_ADDRESS_2);
    u32 io_addr = bar & PCI_BASE_ADDRESS_IO_MASK;
    dprintf(1, "bochs-display: bdf %02x:%02x.%x, bar 0 at 0x%x, bar 1 at 0x%x\n"
            , pci_bdf_to_bus(bdf) , pci_bdf_to_dev(bdf), pci_bdf_to_fn(bdf),
            lfb_addr, io_addr);

    u16 *dispi = (void*)(io_addr + 0x500);
    u8 *vga = (void*)(io_addr + 0x400);
    u16 id = readw(dispi + VBE_DISPI_INDEX_ID);
    dprintf(1, "bochs-display: id is 0x%x, %s\n", id
            , id == VBE_DISPI_ID5 ? "good" : "FAIL");
    if (id != VBE_DISPI_ID5)
        return -1;

     dprintf(1, "bochs-display: using %dx%d, %d bpp (%d stride)\n"
            , FRAMEBUFFER_WIDTH, FRAMEBUFFER_HEIGHT
            , FRAMEBUFFER_BPP * 8, FRAMEBUFFER_STRIDE);

    cbvga_setup_modes(lfb_addr, FRAMEBUFFER_BPP * 8,
                      FRAMEBUFFER_WIDTH, FRAMEBUFFER_HEIGHT,
                      FRAMEBUFFER_STRIDE);

    writew(dispi + VBE_DISPI_INDEX_XRES,   FRAMEBUFFER_WIDTH);
    writew(dispi + VBE_DISPI_INDEX_YRES,   FRAMEBUFFER_HEIGHT);
    writew(dispi + VBE_DISPI_INDEX_BPP,    FRAMEBUFFER_BPP * 8);
    writew(dispi + VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED);

    writeb(vga, 0x20); /* unblank (for qemu -device VGA) */

    return 0;
}
