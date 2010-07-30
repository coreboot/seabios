#ifndef __JPEG_H
#define __JPEG_H

struct jpeg_decdata;
struct jpeg_decdata *jpeg_alloc(void);
int jpeg_decode(unsigned char *, unsigned char *, int, int, int,
                struct jpeg_decdata *);
int jpeg_check_size(struct jpeg_decdata *, unsigned char *, int, int);

#endif
