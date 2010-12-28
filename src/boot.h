// Storage for boot definitions.
#ifndef __BOOT_H
#define __BOOT_H


/****************************************************************
 * Initial Program Load (IPL)
 ****************************************************************/

struct ipl_entry_s {
    u16 type;
    u32 vector;
};

struct ipl_s {
    struct ipl_entry_s bev[8];
    int bevcount;
    int checkfloppysig;
    char **fw_bootorder;
    int fw_bootorder_count;
};

#define IPL_TYPE_FLOPPY      0x01
#define IPL_TYPE_HARDDISK    0x02
#define IPL_TYPE_CDROM       0x03
#define IPL_TYPE_CBFS        0x20
#define IPL_TYPE_BEV         0x80
#define IPL_TYPE_BCV         0x81


/****************************************************************
 * Function defs
 ****************************************************************/

// boot.c
extern struct ipl_s IPL;
void boot_setup(void);
void boot_add_bev(u16 seg, u16 bev, u16 desc);
void boot_add_bcv(u16 seg, u16 ip, u16 desc);
struct drive_s;
void boot_add_floppy(struct drive_s *drive_g);
void boot_add_hd(struct drive_s *drive_g);
void boot_add_cd(struct drive_s *drive_g);
void boot_add_cbfs(void *data, const char *desc);

void boot_prep(void);

#endif // __BOOT_H
