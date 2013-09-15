// Misc function and variable declarations.
#ifndef __UTIL_H
#define __UTIL_H

#include "types.h" // u32

// output.c
extern u16 DebugOutputPort;
void debug_serial_preinit(void);
void panic(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2))) __noreturn;
void printf(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)));
int snprintf(char *str, size_t size, const char *fmt, ...)
    __attribute__ ((format (printf, 3, 4)));
char * znprintf(size_t size, const char *fmt, ...)
    __attribute__ ((format (printf, 2, 3)));
void __dprintf(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)));
struct bregs;
void __debug_enter(struct bregs *regs, const char *fname);
void __debug_isr(const char *fname);
void __debug_stub(struct bregs *regs, int lineno, const char *fname);
void __warn_invalid(struct bregs *regs, int lineno, const char *fname);
void __warn_unimplemented(struct bregs *regs, int lineno, const char *fname);
void __warn_internalerror(int lineno, const char *fname);
void __warn_noalloc(int lineno, const char *fname);
void __warn_timeout(int lineno, const char *fname);
void __set_invalid(struct bregs *regs, int lineno, const char *fname);
void __set_unimplemented(struct bregs *regs, int lineno, const char *fname);
void __set_code_invalid(struct bregs *regs, u32 linecode, const char *fname);
void __set_code_unimplemented(struct bregs *regs, u32 linecode
                              , const char *fname);
void hexdump(const void *d, int len);

#define dprintf(lvl, fmt, args...) do {                         \
        if (CONFIG_DEBUG_LEVEL && (lvl) <= CONFIG_DEBUG_LEVEL)  \
            __dprintf((fmt) , ##args );                         \
    } while (0)
#define debug_enter(regs, lvl) do {                     \
        if ((lvl) && (lvl) <= CONFIG_DEBUG_LEVEL)       \
            __debug_enter((regs), __func__);            \
    } while (0)
#define debug_isr(lvl) do {                             \
        if ((lvl) && (lvl) <= CONFIG_DEBUG_LEVEL)       \
            __debug_isr(__func__);                      \
    } while (0)
#define debug_stub(regs)                        \
    __debug_stub((regs), __LINE__, __func__)
#define warn_invalid(regs)                      \
    __warn_invalid((regs), __LINE__, __func__)
#define warn_unimplemented(regs)                        \
    __warn_unimplemented((regs), __LINE__, __func__)
#define warn_internalerror()                    \
    __warn_internalerror(__LINE__, __func__)
#define warn_noalloc()                          \
    __warn_noalloc(__LINE__, __func__)
#define warn_timeout()                          \
    __warn_timeout(__LINE__, __func__)
#define set_invalid(regs)                       \
    __set_invalid((regs), __LINE__, __func__)
#define set_code_invalid(regs, code)                                    \
    __set_code_invalid((regs), (code) | (__LINE__ << 8), __func__)
#define set_unimplemented(regs)                         \
    __set_unimplemented((regs), __LINE__, __func__)
#define set_code_unimplemented(regs, code)                              \
    __set_code_unimplemented((regs), (code) | (__LINE__ << 8), __func__)

// kbd.c
void kbd_init(void);
void handle_15c2(struct bregs *regs);
void process_key(u8 key);

// mouse.c
void mouse_init(void);
void process_mouse(u8 data);

// serial.c
void serial_setup(void);
void lpt_setup(void);

// clock.c
void clock_setup(void);
void handle_1583(struct bregs *regs);
void handle_1586(struct bregs *regs);
void useRTC(void);
void releaseRTC(void);

// hw/timer.c
void timer_setup(void);
void pmtimer_setup(u16 ioport);
u32 timer_calc(u32 msecs);
u32 timer_calc_usec(u32 usecs);
int timer_check(u32 end);
void ndelay(u32 count);
void udelay(u32 count);
void mdelay(u32 count);
void nsleep(u32 count);
void usleep(u32 count);
void msleep(u32 count);
u32 ticks_to_ms(u32 ticks);
u32 ticks_from_ms(u32 ms);
u32 irqtimer_calc_ticks(u32 count);
u32 irqtimer_calc(u32 msecs);
int irqtimer_check(u32 end);

// apm.c
void apm_shutdown(void);
void handle_1553(struct bregs *regs);

// pcibios.c
void handle_1ab1(struct bregs *regs);
void bios32_init(void);

// fw/shadow.c
void make_bios_writable(void);
void make_bios_readonly(void);
void qemu_prep_reset(void);

// fw/pciinit.c
extern const u8 pci_irqs[4];
void pci_setup(void);

// fw/smm.c
void smm_device_setup(void);
void smm_setup(void);

// fw/smp.c
extern u32 CountCPUs;
extern u32 MaxCountCPUs;
void wrmsr_smp(u32 index, u64 val);
void smp_setup(void);
int apic_id_is_present(u8 apic_id);

// fw/coreboot.c
extern const char *CBvendor, *CBpart;
struct cbfs_file;
void debug_cbmem(char c);
void cbfs_run_payload(struct cbfs_file *file);
void coreboot_platform_setup(void);
void cbfs_payload_setup(void);
void coreboot_preinit(void);
void coreboot_cbfs_init(void);

// fw/biostable.c
void copy_smbios(void *pos);
void copy_table(void *pos);

// vgahooks.c
void handle_155f(struct bregs *regs);
struct pci_device;
void vgahook_setup(struct pci_device *pci);

// optionroms.c
void call_bcv(u16 seg, u16 ip);
int is_pci_vga(struct pci_device *pci);
void optionrom_setup(void);
void vgarom_setup(void);
void s3_resume_vga(void);
extern int ScreenAndDebug;

// bootsplash.c
void enable_vga_console(void);
void enable_bootsplash(void);
void disable_bootsplash(void);

// resume.c
extern int HaveRunPost;
void dma_setup(void);

// pnpbios.c
#define PNP_SIGNATURE 0x506e5024 // $PnP
u16 get_pnp_offset(void);
void pnp_init(void);

// pmm.c
void pmm_init(void);
void pmm_prepboot(void);

// fw/mtrr.c
void mtrr_setup(void);

// romlayout.S
void reset_vector(void) __noreturn;

// misc.c
void mathcp_setup(void);
extern u8 BiosChecksum;

// version (auto generated file out/version.c)
extern const char VERSION[];

#endif // util.h
