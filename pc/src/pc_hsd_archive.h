#ifndef PC_HSD_ARCHIVE_H
#define PC_HSD_ARCHIVE_H
#include <stddef.h>

/* Convert one HSD archive (.dat/.usd) in place from big-endian to host order:
   the header, all three tables, and exactly the body words the relocation
   table names as pointers. Marks the body's archive provenance on success.

   Returns 1 when it converted, 0 when the archive was already in host order,
   and -1 when `file_size` matches neither byte order -- which means this is
   not the archive the caller thinks it is, and nothing was modified. */
int pc_hsd_archive_convert(void* src, size_t file_size);

#endif
