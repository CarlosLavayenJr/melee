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
/* Which schema last claimed the word at `p`: 0 when it is not archive
   memory, 0x80 when it arrived as an archive body and nothing has claimed
   it, otherwise the PC_HSD_* value that did. For diagnostics -- it answers
   "who converted this before I got here" by name rather than by guess. */
unsigned pc_hsd_kind_at(const void* p);
/* Shared field schema: stage map_head and MObj loading both access this word. */
void pc_hsd_mobj_flags_to_native(void* flags);
enum { PC_HSD_POBJ = 1, PC_HSD_VERTEX, PC_HSD_ENVELOPE, PC_HSD_JOINT,
       PC_HSD_MATRIX, PC_HSD_MOBJ, PC_HSD_MATERIAL, PC_HSD_TOBJ,
       PC_HSD_IMAGE, PC_HSD_LOD, PC_HSD_TLUT, PC_HSD_TEV, PC_HSD_WOBJ,
       PC_HSD_PSCMDBANK, PC_HSD_PSCMDLIST, PC_HSD_PSTEXBANK,
       PC_HSD_PSTEXGROUP, PC_HSD_PSFORMBANK, PC_HSD_PSFORMGROUP,
       PC_HSD_MAPCOLL, PC_HSD_STAGEHEAD, PC_HSD_STAGEENTRY,
       PC_HSD_GROUNDPARAM, PC_HSD_STAGEITEM, PC_HSD_STAGEJOINTS,
       PC_HSD_MOBJ_FLAGS, PC_HSD_LIGHT, PC_HSD_LIGHT_POINT,
       PC_HSD_LIGHT_SPOT, PC_HSD_LIGHT_ATTN, PC_HSD_TEXANIM,
       PC_HSD_SHAPESET, PC_HSD_FTATTRS, PC_HSD_FTMOVES,
       PC_HSD_FTDEMOMOVES, PC_HSD_STAGEMAPENTRY, PC_HSD_FTCOMMON, PC_HSD_FTPARTS,
       PC_HSD_FTHIDDENPARTS, PC_HSD_FTMODELS, PC_HSD_FTTOBJIDS,
       PC_HSD_FTVIS, PC_HSD_FTVISRUN,
       PC_HSD_FTHURT, PC_HSD_FTDYNAMICS, PC_HSD_FTEXTATTR,
       PC_HSD_STAGEPARAMS, PC_HSD_ITCOMMON, PC_HSD_ITHURT };
#endif
