// Storage for boot definitions.
#ifndef __BOOT_H
#define __BOOT_H


/****************************************************************
 * Initial Program Load (IPL)
 ****************************************************************/

struct ipl_entry_s {
    u16 type;
    u16 subchoice;
    u32 vector;
    const char *description;
};

struct ipl_s {
    struct ipl_entry_s bev[8];
    struct ipl_entry_s bcv[8];
    int bevcount, bcvcount;
    u32 bootorder;
    int checkfloppysig;
};

#define IPL_TYPE_FLOPPY      0x01
#define IPL_TYPE_HARDDISK    0x02
#define IPL_TYPE_CDROM       0x03
#define IPL_TYPE_CBFS        0x20
#define IPL_TYPE_BEV         0x80

#define BCV_TYPE_EXTERNAL    0x80
#define BCV_TYPE_INTERNAL    0x02


/****************************************************************
 * Function defs
 ****************************************************************/

// boot.c
extern struct ipl_s IPL;
void boot_setup(void);
void add_bev(u16 seg, u16 bev, u16 desc);
void add_bcv(u16 seg, u16 ip, u16 desc);
struct drive_s;
void add_bcv_internal(struct drive_s *drive_g);
void boot_prep(void);

#endif // __BOOT_H
