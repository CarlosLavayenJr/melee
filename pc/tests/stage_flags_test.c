#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <melee/gr/types.h>
int main(void)
{
    StageCallbacks c = {0};
    assert(sizeof c == 20 && offsetof(StageCallbacks, flags) == 16);
    c.flags = 0xc0000000u;
    assert(c.flags_b0 && c.flags_b1 && !c.flags_b2 && !c.flags_b7);
    c.flags = 0x01000000u;
    assert(c.flags_b7 && !c.flags_b0 && !c.flags_b1);
    c.flags = 0x00ffffffu;
    assert(!c.flags_b0 && !c.flags_b1 && !c.flags_b7);
    c.flags_b0 = 1; c.flags_b1 = 1;
    assert(c.flags == 0xc0ffffffu);
    puts("PASS: native stage callback masks and bitfield aliases preserve ABI");
    return 0;
}
