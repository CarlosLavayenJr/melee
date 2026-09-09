/* Synthetic reproduction of the GX API state in sobjlib.c's four-stage
   YUV program, not game data or a replacement for game rendering. */
static unsigned color_op(unsigned a, unsigned b, unsigned c, unsigned d,
                         unsigned sub, unsigned clamp, unsigned scale)
{ return (a<<12)|(b<<8)|(c<<4)|d|(sub<<18)|(clamp<<19)|(scale<<20); }
static unsigned alpha_op(unsigned a, unsigned b, unsigned c, unsigned d,
                         unsigned sub, unsigned clamp)
{ return (a<<13)|(b<<10)|(c<<7)|(d<<4)|(sub<<18)|(clamp<<19); }
static void movie_test_state(void)
{
    unsigned color[4] = {
        color_op(15,8,14,2,0,0,0), color_op(15,8,14,0,0,0,1),
        color_op(15,8,12,0,0,1,0), color_op(1,0,14,15,0,1,0)};
    unsigned alpha[4] = {
        alpha_op(7,4,6,1,1,0), alpha_op(7,4,6,0,1,0),
        alpha_op(4,7,7,0,0,1), alpha_op(7,7,7,7,0,1)};
    pc_gx_bp_write(0x00000c02); /* four stages, two texgens, no culling */
    pc_gx_bp_write(0x283ca3c9); /* stage0 U/coord1; stage1 V/coord1 */
    pc_gx_bp_write(0x293803c0); /* stage2 Y/coord0; stage3 no texture */
    pc_gx_bp_write(0x41000008); /* no blend, RGB writes */
    pc_gx_bp_write(0x4000000f); /* depth ALWAYS */
    pc_gx_bp_write(0xf37f0000); /* alpha ALWAYS OR ALWAYS */
    pc_gx_bp_write(0xf6000004 | (12<<4) | (28<<9) | (13<<14) | (29<<19));
    pc_gx_bp_write(0xf700000e | (14<<14));
    pc_gx_bp_write(0xe20877a6); /* REG0 R=-90, A=135 */
    pc_gx_bp_write(0xe300078e); /* REG0 B=-114, G=0 */
    pc_gx_bp_write(0xe0858000); pc_gx_bp_write(0xe18000e2); /* K0 */
    pc_gx_bp_write(0xe28b60b3); pc_gx_bp_write(0xe3800000); /* K1 */
    pc_gx_bp_write(0xe48800ff); pc_gx_bp_write(0xe58000ff); /* K2 */
    for (unsigned i=0; i<4; ++i) {
        pc_gx_bp_write(((0xc0+i*2)<<24) | color[i]);
        pc_gx_bp_write(((0xc1+i*2)<<24) | alpha[i]);
        pc_gx_bp_write((0x10+i)<<24);
    }
    for (unsigned i=0; i<2; ++i)
        __wrap_GXSetTexCoordGen2(i, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, 0, GX_PTIDENTITY);
}
