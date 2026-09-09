#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pc_hps.h"
int main(void)
{
    unsigned char header[160] = {' ','H','A','L','P','S','T',0};
    unsigned char original[160], block[40];
    unsigned i, n;
    header[10]=0x7d; header[15]=2;
    for(i=16;i<160;i++) header[i]=(unsigned char)i;
    memcpy(original,header,sizeof header);
    assert(!pc_hps_header_to_native(header));
    memcpy(&n,header+8,4); assert(n==32000);
    memcpy(&n,header+12,4); assert(n==2);
    assert(header[16]==17 && header[17]==16);
    assert(!memcmp(header+128,original+128,32));
    memcpy(header,original,sizeof header); header[15]=3;
    assert(pc_hps_header_to_native(header)==-1);
    for(i=0;i<40;i++) block[i]=(unsigned char)i;
    pc_hps_block_to_native(block);
    assert(block[0]==3 && block[3]==0 && block[8]==11);
    assert(block[12]==13 && block[13]==12 && block[20]==21);
    assert(block[18]==18 && block[19]==19 && block[26]==26);
    assert(block[32]==32 && block[39]==39);
    puts("PASS: HPS channel/rate, AX halfwords, block metadata, payload preservation, invalid channels");
    return 0;
}
