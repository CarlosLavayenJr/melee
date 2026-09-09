#include <assert.h>
#include <stdio.h>
#include <sysdolphin/baselib/card_host_storage.h>
HSD_CardHostStorage hsd_card_host_storage;
int main(void)
{
    unsigned i;
    for (i = 0; i < 128; ++i) {
        hsd_804D1148[i][0] = i + 1;
        assert(*(u32*)(hsd_804D1138 + 0x10 + i * 0x24) == i + 1);
    }
    hsd_804D2348[0] = 17;
    assert(hsd_804D1138[0x1210] == 17);
    assert((unsigned char*)&hsd_804D1148[128] == hsd_804D1138 + 0x1210);
    puts("PASS: native card header, 128 command entries and pending queue share one layout");
    return 0;
}
