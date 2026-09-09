#ifndef PC_HSD_ENDIAN_H
#define PC_HSD_ENDIAN_H
#include <stddef.h>
/* Track freshly loaded archive bodies in the port's 24 MiB cached RAM.
   A schema is applied once per object and reset when DVD overwrites it. */
void pc_hsd_forget_range(void* data, size_t size);
void pc_hsd_archive_body(void* data, size_t size);
int pc_hsd_claim(void* data, size_t size, unsigned kind);
/* Whether every word of a range lies inside memory a DVD read delivered and
   pc_hsd_archive_body marked. Schemas whose counts and offsets come out of the
   data itself use this to check a derived pointer before following it. */
int pc_hsd_in_archive(const void* data, size_t size);
enum { PC_HSD_POBJ = 1, PC_HSD_VERTEX, PC_HSD_ENVELOPE, PC_HSD_JOINT,
       PC_HSD_MATRIX, PC_HSD_MOBJ, PC_HSD_MATERIAL, PC_HSD_TOBJ,
       PC_HSD_IMAGE, PC_HSD_LOD, PC_HSD_TLUT, PC_HSD_TEV, PC_HSD_WOBJ,
       PC_HSD_PSCMDBANK, PC_HSD_PSCMDLIST, PC_HSD_PSTEXBANK,
       PC_HSD_PSTEXGROUP, PC_HSD_PSFORMBANK, PC_HSD_PSFORMGROUP,
       PC_HSD_MAPCOLL, PC_HSD_STAGEHEAD, PC_HSD_STAGEENTRY,
       PC_HSD_GROUNDPARAM, PC_HSD_STAGEITEM, PC_HSD_STAGEJOINTS };
#endif
