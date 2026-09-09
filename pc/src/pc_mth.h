#ifndef PC_MTH_H
#define PC_MTH_H
static unsigned pc_mth_u32(const void* data)
{
    const unsigned char* p=data;
    return (unsigned)p[0]<<24 | (unsigned)p[1]<<16 | (unsigned)p[2]<<8 | p[3];
}
/* Validate before mutating: fixed 0x40-byte MTHP file header. */
static int pc_mth_header_to_native(void* data)
{
    unsigned char* p=data;
    unsigned i, w=pc_mth_u32(p+16), h=pc_mth_u32(p+20);
    unsigned size=pc_mth_u32(p+12), frames=pc_mth_u32(p+28);
    if(p[0]!='M'||p[1]!='T'||p[2]!='H'||p[3]!='P' || pc_mth_u32(p+8)!=2 ||
       !w || !h || w>640 || h>480 || (w&15) || (h&15) ||
       !size || size>0x20000 || !frames || frames>100000 ||
       pc_mth_u32(p+32)<0x40 || pc_mth_u32(p+36)!=0 ||
       !pc_mth_u32(p+40) || pc_mth_u32(p+40)>size) return -1;
    for(i=4;i<=40;i+=4) {
        unsigned v=pc_mth_u32(p+i);
        p[i]=(unsigned char)v; p[i+1]=(unsigned char)(v>>8);
        p[i+2]=(unsigned char)(v>>16); p[i+3]=(unsigned char)(v>>24);
    }
    return 0;
}
#endif
