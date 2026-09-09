#ifndef HSD_CARD_HOST_STORAGE_H
#define HSD_CARD_HOST_STORAGE_H
#include <dolphin/types.h>
#include <stddef.h>
/* The console linker places these three symbols adjacently. Card code treats
   them as one context using base+0x10/base+0x1210. Native linkers may reorder
   independent globals, so express the actual storage layout as one object. */
typedef struct HSD_CardHostStorage {
    u8 header[0x10];
    u32 commands[128][9];
    u8 pending[0x300];
} HSD_CardHostStorage;
extern HSD_CardHostStorage hsd_card_host_storage;
_Static_assert(offsetof(HSD_CardHostStorage, commands) == 0x10, "card command offset");
_Static_assert(offsetof(HSD_CardHostStorage, pending) == 0x1210, "card pending offset");
_Static_assert(sizeof(HSD_CardHostStorage) == 0x1510, "card context size");
#define hsd_804D1138 ((u8*)&hsd_card_host_storage)
#define hsd_804D1148 (hsd_card_host_storage.commands)
#define hsd_804D2348 (hsd_card_host_storage.pending)
#endif
