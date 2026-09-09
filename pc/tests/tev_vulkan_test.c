/* GPU readback of synthetic TEV inputs only; never used by the game runtime. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "../src/pc_gx_fifo.c"
#include "../src/pc_gx_material.c"
#include "tev_movie_state.h"

void __real_GXSetVtxDesc(GXAttr a, GXAttrType t) { (void)a; (void)t; }
void __real_GXClearVtxDesc(void) {}
void __real_GXSetVtxAttrFmt(GXVtxFmt f, GXAttr a, GXCompCnt c, GXCompType t, u8 n)
{ (void)f; (void)a; (void)c; (void)t; (void)n; }
void __real_GXSetArray(GXAttr a, const void* p, u8 s) { (void)a; (void)p; (void)s; }
void __real_GXSetTexCoordGen2(GXTexCoordID i, GXTexGenType t, GXTexGenSrc s, u32 m, GXBool n, u32 p)
{ (void)i; (void)t; (void)s; (void)m; (void)n; (void)p; }

static void quad(VkCommandBuffer cmd, unsigned index)
{
    float left = -1 + index * 0.25f, right = left + 0.25f;
    /* Same world-space winding as sobjlib.c: top-left, top-right, bottom-right. */
    const float xy[6][2] = {{left,1},{right,1},{right,-1},{right,-1},{left,-1},{left,1}};
    memset(vertex_buf, 0, sizeof(pc_vertex_t)*6);
    for (unsigned i=0; i<6; ++i) {
        vertex_buf[i].pos[0] = xy[i][0]; vertex_buf[i].pos[1] = xy[i][1];
        vertex_buf[i].uv[0] = vertex_buf[i].uv[1] = 0.5f;
    }
    flush_draw(cmd, 6);
}
static int clamp8(float n)
{ return n < 0 ? 0 : n > 255 ? 255 : (int)(n + 0.5f); }

int main(void)
{
    const unsigned char samples[4][3] = {{16,128,128},{235,128,128},{81,90,240},{100,200,60}};
    const char* path = "build/tev-vulkan-test.bmp";
    VkSamplerCreateInfo sampler = {0};
    VkCommandBuffer cmd;
    unsigned w, h;
    unsigned char header[54], pixel[4];
    FILE* file;
    SetEnvironmentVariableA("PC_CAPTURE_FRAME", path);
    assert(!pc_vulkan_init(256, 64, "Synthetic TEV regression test"));
    cmd = pc_vulkan_begin_frame(); assert(cmd);
    pc_gx_fifo_begin_frame(); pc_gx_textures_begin_frame();
    memset(proj_mtx, 0, sizeof proj_mtx);
    for (unsigned i=0; i<4; ++i) proj_mtx[i][i] = 1;
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    movie_test_state();
    for (unsigned i=0; i<4; ++i) {
        for (unsigned slot=0; slot<3; ++slot) {
            unsigned char tile[32]; memset(tile, samples[i][slot], sizeof tile);
            assert(!pc_gx_texture_load(slot, 1, 8, 4, 1, tile, sizeof tile, &sampler));
        }
        quad(cmd, i);
    }
    /* Different uniform contents in the same submission must not replace earlier draws. */
    pc_gx_bp_write(0x00000000);
    pc_gx_bp_write(0xc008fff2); pc_gx_bp_write(0xc108fff0);
    pc_gx_bp_write(0xe20000ff); pc_gx_bp_write(0xe3000000); /* red */
    quad(cmd, 4);
    pc_gx_bp_write(0xe2000000); pc_gx_bp_write(0xe30ff000); /* green */
    quad(cmd, 5);
    pc_gx_bp_write(0x00004000); /* GX back culling: sprite front remains visible */
    quad(cmd, 6);
    pc_gx_bp_write(0x00008000); /* GX front culling: same input disappears */
    quad(cmd, 7);
    pc_vulkan_end_frame();
    assert(vkDeviceWaitIdle(pc_vulkan_device()) == VK_SUCCESS);
    pc_vulkan_extent(&w, &h);
    file = fopen(path, "rb"); assert(file);
    assert(fread(header, 1, sizeof header, file) == sizeof header);
    assert(header[0]=='B' && header[1]=='M' && header[28]==32);
    for (unsigned i=0; i<8; ++i) {
        int expected[3] = {0,0,0};
        unsigned x = (2*i+1)*w/16, y=h/2;
        assert(!fseek(file, 54+(y*w+x)*4, SEEK_SET));
        assert(fread(pixel, 1, 4, file)==4);
        if (i<4) {
            float yy=samples[i][0], u=samples[i][1], v=samples[i][2];
            expected[0]=clamp8(yy+2*(-90+v*179/255.0f));
            expected[1]=clamp8(yy+135-u*88/255.0f-v*182/255.0f);
            expected[2]=clamp8(yy+2*(-114+u*226/255.0f));
        } else if (i==4) expected[0]=255;
        else if (i==5 || i==6) expected[1]=255;
        for (unsigned c=0; c<3; ++c) {
            printf("sample %u channel %u: got %u expected %d\n", i,c,pixel[2-c],expected[c]);
            assert(abs((int)pixel[2-c]-expected[c]) <= 2);
        }
    }
    fclose(file);
    puts("PASS: GPU four-stage TEV, three textures, immutable per-draw state and GX face culling");
    return 0;
}
