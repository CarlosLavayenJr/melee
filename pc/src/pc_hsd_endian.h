#ifndef PC_HSD_ENDIAN_H
#define PC_HSD_ENDIAN_H
#include <stddef.h>
/* Track freshly loaded archive bodies in the port's 24 MiB cached RAM.
   A schema is applied once per object and reset when DVD overwrites it. */
void pc_hsd_forget_range(void* data, size_t size);
void pc_hsd_archive_body(void* data, size_t size);
int pc_hsd_claim(void* data, size_t size, unsigned kind);
enum { PC_HSD_POBJ = 1, PC_HSD_VERTEX, PC_HSD_ENVELOPE, PC_HSD_JOINT,
       PC_HSD_MATRIX, PC_HSD_MOBJ, PC_HSD_MATERIAL, PC_HSD_TOBJ,
       PC_HSD_IMAGE, PC_HSD_LOD, PC_HSD_TLUT, PC_HSD_TEV, PC_HSD_WOBJ };
#endif
