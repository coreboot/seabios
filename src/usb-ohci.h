#ifndef __USB_OHCI_H
#define __USB_OHCI_H

// usb-ohci.c
struct usb_s;
int ohci_init(struct usb_s *cntl);
int ohci_control(u32 endp, int dir, const void *cmd, int cmdsize
                 , void *data, int datasize);
struct usb_pipe *ohci_alloc_intr_pipe(u32 endp, int period);
int ohci_poll_intr(void *pipe, void *data);


/****************************************************************
 * ohci structs and flags
 ****************************************************************/

struct ohci_ed {
    u32 hwINFO;
    u32 hwTailP;
    u32 hwHeadP;
    u32 hwNextED;
} PACKED;

#define ED_ISO          (1 << 15)
#define ED_SKIP         (1 << 14)
#define ED_LOWSPEED     (1 << 13)
#define ED_OUT          (0x01 << 11)
#define ED_IN           (0x02 << 11)

#define ED_C            (0x02)
#define ED_H            (0x01)

struct ohci_td {
    u32 hwINFO;
    u32 hwCBP;
    u32 hwNextTD;
    u32 hwBE;
} PACKED;

#define TD_CC       0xf0000000
#define TD_CC_GET(td_p) ((td_p >>28) & 0x0f)
#define TD_DI       0x00E00000
#define TD_DI_SET(X) (((X) & 0x07)<< 21)

#define TD_DONE     0x00020000
#define TD_ISO      0x00010000

#define TD_EC       0x0C000000
#define TD_T        0x03000000
#define TD_T_DATA0  0x02000000
#define TD_T_DATA1  0x03000000
#define TD_T_TOGGLE 0x00000000
#define TD_DP       0x00180000
#define TD_DP_SETUP 0x00000000
#define TD_DP_IN    0x00100000
#define TD_DP_OUT   0x00080000

#define TD_R        0x00040000

struct ohci_hcca {
    u32  int_table[32];
    u32  frame_no;
    u32  done_head;
    u8   reserved[120];
} PACKED;

struct ohci_regs {
    u32  revision;
    u32  control;
    u32  cmdstatus;
    u32  intrstatus;
    u32  intrenable;
    u32  intrdisable;

    u32  hcca;
    u32  ed_periodcurrent;
    u32  ed_controlhead;
    u32  ed_controlcurrent;
    u32  ed_bulkhead;
    u32  ed_bulkcurrent;
    u32  donehead;

    u32  fminterval;
    u32  fmremaining;
    u32  fmnumber;
    u32  periodicstart;
    u32  lsthresh;

    u32  roothub_a;
    u32  roothub_b;
    u32  roothub_status;
    u32  roothub_portstatus[15];
} PACKED;

#define OHCI_INTR_MIE   (1 << 31)

#endif // usb-ohci.h
