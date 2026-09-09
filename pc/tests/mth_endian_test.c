#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pc_mth.h"
static void be(unsigned char* p, unsigned v)
{ p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
int main(void)
{
    unsigned char raw[64]={ 'M','T','H','P' }, copy[64];
    unsigned value;
    be(raw+8,2); be(raw+12,0xeee0); be(raw+16,640); be(raw+20,480);
    be(raw+24,30); be(raw+28,3036); be(raw+32,64); be(raw+40,0x1e00);
    memcpy(copy,raw,64);
    assert(!pc_mth_header_to_native(copy));
    memcpy(&value,copy+16,4); assert(value==640);
    memcpy(&value,copy+20,4); assert(value==480);
    assert(!memcmp(copy,raw,4) && !memcmp(copy+44,raw+44,20));
    be(raw+16,0xffffffff); memcpy(copy,raw,64);
    assert(pc_mth_header_to_native(copy)==-1 && !memcmp(copy,raw,64));
    be(raw+16,640); be(raw+40,0xfffffff0); memcpy(copy,raw,64);
    assert(pc_mth_header_to_native(copy)==-1);
    be(raw,0x1e00); assert(pc_mth_u32(raw)==0x1e00);
    puts("PASS: MTH dimensions, frame sizes, byte preservation and invalid-header rejection");
    return 0;
}
