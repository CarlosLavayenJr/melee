/* Include the decoder to inspect decoded CPU vertices without mocking game
   content. Vulkan is linked only for symbols; NULL command buffers ensure
   this test never initializes a GPU or substitutes a gameplay frame. */
#include <assert.h>
#include <stdio.h>
#include "../src/pc_gx_fifo.c"

void pc_sys_log(const char* s) { fputs(s, stderr); }
VkDevice pc_vulkan_device(void) { return VK_NULL_HANDLE; }
void pc_vulkan_mark_draw(void) {}
VkPhysicalDevice pc_vulkan_physical_device(void) { return VK_NULL_HANDLE; }
VkRenderPass pc_vulkan_render_pass(void) { return VK_NULL_HANDLE; }
void pc_vulkan_extent(unsigned* w, unsigned* h) { *w = 640; *h = 480; }
int pc_gx_material_get(pc_gx_material* out) { (void)out; assert(0); return 0; }
void pc_gx_bp_write(unsigned int value) { (void)value; }
int pc_gx_texture_get(unsigned slot, pc_gx_texture_binding* out) { (void)slot; (void)out; assert(0); return 0; }
int pc_gx_texture_load(unsigned slot, unsigned fmt, unsigned w, unsigned h, unsigned levels,
    const void* source, size_t size, const VkSamplerCreateInfo* sampler)
{ (void)slot; (void)fmt; (void)w; (void)h; (void)levels; (void)source; (void)size; (void)sampler; assert(0); return -1; }
void __real_GXSetVtxDesc(GXAttr a, GXAttrType t) { (void)a; (void)t; }
void __real_GXClearVtxDesc(void) {}
void __real_GXSetVtxAttrFmt(GXVtxFmt f, GXAttr a, GXCompCnt c, GXCompType t, u8 n)
{ (void)f; (void)a; (void)c; (void)t; (void)n; }
void __real_GXSetArray(GXAttr a, const void* p, u8 s) { (void)a; (void)p; (void)s; }

int main(void)
{
    float coords[4][5] = {{1,2,3,0,0}, {4,5,6,1,0}, {7,8,9,1,1}, {10,11,12,0,1}};
    const unsigned indices[6] = {0,1,2,2,3,0};
    unsigned i, c;
    __wrap_GXClearVtxDesc();
    __wrap_GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    __wrap_GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    __wrap_GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    __wrap_GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    pc_gx_immediate_begin(VK_NULL_HANDLE, GX_QUADS, 0, 4);
    for (i = 0; i < 4; ++i) for (c = 0; c < 5; ++c)
        pc_gx_immediate_write(&coords[i][c], 4);
    assert(immediate_size == 83 && immediate_expected == 0);
    for (i = 0; i < 6; ++i) for (c = 0; c < 3; ++c)
        assert(vertex_buf[i].pos[c] == coords[indices[i]][c]);
    for (i = 0; i < 6; ++i) for (c = 0; c < 2; ++c)
        assert(vertex_buf[i].uv[c] == coords[indices[i]][c+3]);
    pc_gx_immediate_end();
    pc_gx_immediate_begin(VK_NULL_HANDLE, GX_QUADS, 0, 4);
    pc_gx_immediate_write(&coords[0][0], 4);
    pc_gx_immediate_end(); assert(immediate_expected == 0);
    /* Real GX color enum values, not the old off-by-one literals. */
    assert(comp_type_size(GX_VA_CLR0, GX_RGB8) == 3);
    assert(comp_type_size(GX_VA_CLR0, GX_RGBX8) == 4);
    assert(comp_type_size(GX_VA_CLR0, GX_RGBA4) == 2);
    {
        unsigned char bytes[4] = {10,20,30,99};
        float color[4]; reader_t r = {bytes, bytes+4, 0};
        read_color(&r, GX_RGBX8, color);
        assert(r.p == bytes+4 && color[0] == 10/255.0f && color[3] == 1.0f);
    }
    {
        unsigned char bytes[3] = {0xf8, 0x1f, 0};
        float color[4]; reader_t r = {bytes, bytes+2, 0};
        read_color(&r, GX_RGB565, color);
        assert(!r.overrun && color[0]==1 && color[1]==0 && color[2]==1 && color[3]==1);
        bytes[0]=0x12; bytes[1]=0x34; r=(reader_t){bytes,bytes+2,0};
        read_color(&r, GX_RGBA4, color);
        assert(color[0]==17/255.0f && color[1]==34/255.0f && color[2]==51/255.0f && color[3]==68/255.0f);
        bytes[0]=0x04; bytes[1]=0x2a; bytes[2]=0xbf; r=(reader_t){bytes,bytes+3,0};
        read_color(&r, GX_RGBA6, color);
        assert(color[0]==4/255.0f && color[1]==8/255.0f && color[2]==170/255.0f && color[3]==1);
        r=(reader_t){bytes,bytes+2,0}; read_color(&r,GX_RGBA6,color); assert(r.overrun);
    }
    puts("PASS: immediate floats/UVs, quad expansion, truncation and packed vertex colors");
    return 0;
}
