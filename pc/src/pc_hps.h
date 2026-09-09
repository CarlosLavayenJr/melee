#ifndef PC_HPS_H
#define PC_HPS_H
/* HPS stream metadata consumed by synth.c: 0x80-byte file header (two u32
   values plus up to two 0x38-byte AX address/ADPCM records), and 0x20-byte
   block headers. Encoded ADPCM payloads are deliberately untouched. */
static unsigned pc_hps_be32(const unsigned char* p)
{ return (unsigned)p[0]<<24 | (unsigned)p[1]<<16 | (unsigned)p[2]<<8 | p[3]; }
static void pc_hps_swap32(unsigned char* p)
{ unsigned char a=p[0], b=p[1]; p[0]=p[3]; p[1]=p[2]; p[2]=b; p[3]=a; }
static void pc_hps_swap16(unsigned char* p)
{ unsigned char a=p[0]; p[0]=p[1]; p[1]=a; }
static int pc_hps_header_to_native(void* data)
{
    unsigned char* p=data;
    unsigned i, channels=pc_hps_be32(p+12), rate=pc_hps_be32(p+8);
    static const unsigned char magic[8]={' ','H','A','L','P','S','T',0};
    for(i=0;i<8;i++) if(p[i]!=magic[i]) return -1;
    if ((channels != 1 && channels != 2) || !rate || rate > 96000) return -1;
    pc_hps_swap32(p+8); pc_hps_swap32(p+12);
    for(i=16;i<16+channels*56;i+=2) pc_hps_swap16(p+i);
    return 0;
}
static void pc_hps_block_to_native(void* data)
{
    unsigned char* p=data;
    unsigned i, channel;
    for(i=0;i<12;i+=4) pc_hps_swap32(p+i);
    for(channel=0;channel<2;channel++)
        for(i=0;i<6;i+=2) pc_hps_swap16(p+12+channel*8+i);
}
#endif
