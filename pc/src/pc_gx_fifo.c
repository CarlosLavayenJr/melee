/* pc_gx_fifo.c — decodes a GX command stream into real Vulkan draws.
 *
 * pc/GX_RENDERER.md explains why this file has to exist at all: a display
 * list is a pre-recorded FIFO command stream, and it reaches
 * GXCallDisplayList as an opaque (void*, u32) -- no amount of intercepting
 * individual GX* calls decodes what's inside one. This does, for the subset
 * confirmed needed so far: position and one vertex color, direct or
 * indexed, no textures or TEV yet (those are later milestones).
 *
 * Every opcode and register offset below is cross-checked against two
 * independent sources that have to agree, not read from either alone: the
 * game's own extern/dolphin/include/dolphin/gx/GXCommandList.h (opcodes,
 * masks) and GXEnum.h (attribute/component enums), and encounter/aurora's
 * lib/gx/{command_processor,regs}.cpp (MIT licensed) for the register
 * bit-layouts and per-opcode byte lengths neither Nintendo header states
 * outright. Getting a byte-length wrong desyncs everything after it in the
 * stream, so this follows Aurora's decode closely enough in places to call
 * it a port of that specific logic, not independent work -- credited here
 * rather than left to look coincidental.
 *
 * What this does NOT decode: LOAD_INDX_{A,B,C,D} (indexed XF loads, used
 * for skinning matrices -- skipped, logged once), nested CALL_DL (skipped,
 * logged), and any vertex color format other than RGBA8/RGB8 (logged,
 * rendered white rather than guessed). All are read past correctly -- the
 * stream never desyncs from them -- just not acted on yet.
 */
#include "pc_sys.h"
#include "pc_vulkan.h"

#include <dolphin/gx.h>
#include <string.h>

#include "gx_basic_vert_spv.h"
#include "gx_basic_frag_spv.h"

/* ---- vertex descriptor / format shadow state ----------------------------
 * Populated by __wrap_GXSetVtxDesc/__wrap_GXSetVtxAttrFmt/__wrap_GXSetArray
 * below -- ordinary GX* calls outside any display list, which is how Melee
 * sets these up (matches --trace-gx's own count: format changes are rare
 * next to how often GXCallDisplayList itself is hit). LOAD_CP_REG inside a
 * display list -- rarer, but real -- updates the same tables via
 * handle_cp_reg(), so both paths agree on where state lives. */
typedef struct {
    unsigned char cnt;  /* raw GXCompCnt */
    unsigned char type; /* raw GXCompType */
    unsigned char frac;
} pc_vat_attr_t;

typedef struct {
    pc_vat_attr_t attrs[GX_VA_MAX_ATTR];
} pc_vat_fmt_t;

typedef struct {
    const unsigned char* base;
    unsigned int stride;
} pc_gx_array_t;

static unsigned char vtx_desc[GX_VA_MAX_ATTR]; /* raw GXAttrType per attr */
static pc_vat_fmt_t vtx_fmt[8];                /* GX_VTXFMT0..7 */
static pc_gx_array_t gx_arrays[GX_VA_MAX_ATTR];

/* Matrices, captured from the same calls pc_gx_render.c already wraps.
   GX stores the position matrix as 3 rows of 4 (the implicit 4th row is
   [0,0,0,1]) and the projection matrix as a full 4x4; both row-major, the
   order the game writes them in. */
static float pos_mtx[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
static float proj_mtx[4][4] = {
    {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

void pc_gx_fifo_set_pos_mtx(float m[3][4]) { memcpy(pos_mtx, m, sizeof pos_mtx); }
void pc_gx_fifo_set_proj_mtx(float m[4][4]) { memcpy(proj_mtx, m, sizeof proj_mtx); }

void __real_GXSetVtxDesc(GXAttr attr, GXAttrType type);
void __wrap_GXSetVtxDesc(GXAttr attr, GXAttrType type)
{
    if ((unsigned) attr < GX_VA_MAX_ATTR) {
        vtx_desc[attr] = (unsigned char) type;
    }
    __real_GXSetVtxDesc(attr, type);
}

void __real_GXClearVtxDesc(void);
void __wrap_GXClearVtxDesc(void)
{
    memset(vtx_desc, GX_NONE, sizeof vtx_desc);
    __real_GXClearVtxDesc();
}

void __real_GXSetVtxAttrFmt(GXVtxFmt fmt, GXAttr attr, GXCompCnt cnt,
                            GXCompType type, u8 frac);
void __wrap_GXSetVtxAttrFmt(GXVtxFmt fmt, GXAttr attr, GXCompCnt cnt,
                            GXCompType type, u8 frac)
{
    if ((unsigned) fmt < 8 && (unsigned) attr < GX_VA_MAX_ATTR) {
        vtx_fmt[fmt].attrs[attr].cnt = (unsigned char) cnt;
        vtx_fmt[fmt].attrs[attr].type = (unsigned char) type;
        vtx_fmt[fmt].attrs[attr].frac = frac;
    }
    __real_GXSetVtxAttrFmt(fmt, attr, cnt, type, frac);
}

void __real_GXSetArray(GXAttr attr, const void* base_ptr, u8 stride);
void __wrap_GXSetArray(GXAttr attr, const void* base_ptr, u8 stride)
{
    if ((unsigned) attr < GX_VA_MAX_ATTR) {
        gx_arrays[attr].base = (const unsigned char*) base_ptr;
        gx_arrays[attr].stride = stride;
    }
    __real_GXSetArray(attr, base_ptr, stride);
}

/* ---- byte reader ---------------------------------------------------------- */

typedef struct {
    const unsigned char* p;
    const unsigned char* end;
    int overrun;
} reader_t;

static unsigned char rd_u8(reader_t* r)
{
    if (r->p >= r->end) { r->overrun = 1; return 0; }
    return *r->p++;
}
static unsigned short rd_u16(reader_t* r)
{
    unsigned short a = rd_u8(r), b = rd_u8(r);
    return (unsigned short) ((a << 8) | b);
}
static unsigned int rd_u32(reader_t* r)
{
    unsigned int a = rd_u16(r), b = rd_u16(r);
    return (a << 16) | b;
}
static float rd_f32(reader_t* r)
{
    unsigned int v = rd_u32(r);
    float f;
    memcpy(&f, &v, 4);
    return f;
}
static void rd_skip(reader_t* r, unsigned int n)
{
    if ((unsigned int) (r->end - r->p) < n) { r->overrun = 1; r->p = r->end; return; }
    r->p += n;
}

/* ---- CP register decode (only reached for LOAD_CP_REG inside a display
   list -- Melee mostly sets these via GXSetVtxDesc/GXSetVtxAttrFmt above,
   but not always). Bit layout: Aurora lib/gx/regs.cpp cp_vcd_lo/hi/
   cp_vat_a/b/c, cross-checked against the register offsets
   (0x50/0x60/0x70/0x80/0x90) that are also visible in real hardware docs
   this project has no copy of, so Aurora is the source of truth here. */
static unsigned int reg_bits(unsigned int v, unsigned int size, unsigned int shift)
{
    return (v >> shift) & ((1u << size) - 1u);
}

static void handle_cp_reg(unsigned char addr, unsigned int value)
{
    if (addr == 0x50) { /* VCD low */
        vtx_desc[GX_VA_PNMTXIDX] = (unsigned char) reg_bits(value, 1, 0);
        vtx_desc[GX_VA_TEX0MTXIDX] = (unsigned char) reg_bits(value, 1, 1);
        vtx_desc[GX_VA_TEX1MTXIDX] = (unsigned char) reg_bits(value, 1, 2);
        vtx_desc[GX_VA_TEX2MTXIDX] = (unsigned char) reg_bits(value, 1, 3);
        vtx_desc[GX_VA_TEX3MTXIDX] = (unsigned char) reg_bits(value, 1, 4);
        vtx_desc[GX_VA_TEX4MTXIDX] = (unsigned char) reg_bits(value, 1, 5);
        vtx_desc[GX_VA_TEX5MTXIDX] = (unsigned char) reg_bits(value, 1, 6);
        vtx_desc[GX_VA_TEX6MTXIDX] = (unsigned char) reg_bits(value, 1, 7);
        vtx_desc[GX_VA_TEX7MTXIDX] = (unsigned char) reg_bits(value, 1, 8);
        vtx_desc[GX_VA_POS] = (unsigned char) reg_bits(value, 2, 9);
        vtx_desc[GX_VA_NRM] = (unsigned char) reg_bits(value, 2, 11);
        vtx_desc[GX_VA_CLR0] = (unsigned char) reg_bits(value, 2, 13);
        vtx_desc[GX_VA_CLR1] = (unsigned char) reg_bits(value, 2, 15);
    } else if (addr == 0x60) { /* VCD high */
        int i;
        for (i = 0; i < 8; i++) {
            vtx_desc[GX_VA_TEX0 + i] = (unsigned char) reg_bits(value, 2, (unsigned) i * 2);
        }
    } else if (addr >= 0x70 && addr <= 0x77) { /* VAT A */
        pc_vat_fmt_t* vf = &vtx_fmt[addr - 0x70];
        vf->attrs[GX_VA_POS].cnt = (unsigned char) reg_bits(value, 1, 0);
        vf->attrs[GX_VA_POS].type = (unsigned char) reg_bits(value, 3, 1);
        vf->attrs[GX_VA_POS].frac = (unsigned char) reg_bits(value, 5, 4);
        vf->attrs[GX_VA_NRM].type = (unsigned char) reg_bits(value, 3, 10);
        vf->attrs[GX_VA_CLR0].cnt = (unsigned char) reg_bits(value, 1, 13);
        vf->attrs[GX_VA_CLR0].type = (unsigned char) reg_bits(value, 3, 14);
        vf->attrs[GX_VA_CLR1].cnt = (unsigned char) reg_bits(value, 1, 17);
        vf->attrs[GX_VA_CLR1].type = (unsigned char) reg_bits(value, 3, 18);
        vf->attrs[GX_VA_TEX0].cnt = (unsigned char) reg_bits(value, 1, 21);
        vf->attrs[GX_VA_TEX0].type = (unsigned char) reg_bits(value, 3, 22);
        vf->attrs[GX_VA_TEX0].frac = (unsigned char) reg_bits(value, 5, 25);
    }
    /* VAT B/C (0x80-0x97, TEX1-TEX7) and the array-stride bank (0xB0-0xBF)
       are not decoded: nothing reachable yet uses more than TEX0, and an
       unsupported texcoord format is reported where it is actually read
       (the texture-upload milestone), not here. */
}

/* ---- component sizing, ported from Aurora's comp_type_size/comp_cnt_count
   (lib/gx/attr_fmt.cpp) -- same reasoning as the register bit layout: this
   is the one place a wrong answer desyncs the whole rest of the stream, so
   it is checked against their table rather than derived fresh. */
static unsigned int comp_type_size(GXAttr attr, unsigned char type)
{
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        switch (type) {
        case GX_RGB565: case GX_RGBA4: return 2;
        case GX_RGB8: case GX_RGBA6: return 3;
        case GX_RGBX8: case GX_RGBA8: return 4;
        default: return 4;
        }
    }
    switch (type) {
    case GX_U8: case GX_S8: return 1;
    case GX_U16: case GX_S16: return 2;
    case GX_F32: return 4;
    default:
        pc_sys_log("pc_gx_fifo: unsupported component type; stream may "
                   "desync from here\n");
        return 4;
    }
}

static unsigned int comp_cnt_count(GXAttr attr, unsigned char cnt)
{
    if (attr == GX_VA_POS) return cnt == 0 ? 2 : 3; /* GX_POS_XY : GX_POS_XYZ */
    if (attr == GX_VA_NRM) return cnt == 0 ? 3 : 9; /* XYZ : NBT(3) */
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) return 1;
    if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) return cnt == 0 ? 1 : 2;
    return 1; /* *MTXIDX */
}

static unsigned int attr_direct_size(GXAttr attr, const pc_vat_fmt_t* vf)
{
    if (attr == GX_VA_PNMTXIDX || (attr >= GX_VA_TEX0MTXIDX && attr <= GX_VA_TEX7MTXIDX)) {
        return 1; /* matrix indices are always a single byte, no VAT entry */
    }
    return comp_type_size(attr, vf->attrs[attr].type) *
           comp_cnt_count(attr, vf->attrs[attr].cnt);
}

/* ---- draw output: a growable interleaved (vec3 pos, vec4 color) buffer,
   flushed to Vulkan by pc_gx_fifo_draw() below. */
typedef struct {
    float pos[3];
    float color[4];
} pc_vertex_t;

#define PC_MAX_VERTS 65536
static pc_vertex_t vertex_buf[PC_MAX_VERTS];
static unsigned frame_vertices;

void pc_gx_fifo_begin_frame(void) { frame_vertices = 0; }

static void mtx_transform(const float m[3][4], const float in[3], float out[3])
{
    int i;
    for (i = 0; i < 3; i++) {
        out[i] = m[i][0] * in[0] + m[i][1] * in[1] + m[i][2] * in[2] + m[i][3];
    }
}

static void read_color(reader_t* r, unsigned char type, float out[4])
{
    switch (type) {
    case GX_RGB8: {
        unsigned int r8 = rd_u8(r), g8 = rd_u8(r), b8 = rd_u8(r);
        out[0] = (float) r8 / 255.0f;
        out[1] = (float) g8 / 255.0f;
        out[2] = (float) b8 / 255.0f;
        out[3] = 1.0f;
        break;
    }
    case GX_RGBX8: {
        unsigned int r8 = rd_u8(r), g8 = rd_u8(r), b8 = rd_u8(r);
        rd_u8(r);
        out[0] = (float) r8 / 255.0f;
        out[1] = (float) g8 / 255.0f;
        out[2] = (float) b8 / 255.0f;
        out[3] = 1.0f;
        break;
    }
    case GX_RGBA8: {
        unsigned int r8 = rd_u8(r), g8 = rd_u8(r), b8 = rd_u8(r), a8 = rd_u8(r);
        out[0] = (float) r8 / 255.0f;
        out[1] = (float) g8 / 255.0f;
        out[2] = (float) b8 / 255.0f;
        out[3] = (float) a8 / 255.0f;
        break;
    }
    default: {
        static int warned;
        /* RGB565/RGBA4/RGBA6 -- bit-packed formats not decoded yet. Skip
           the right number of bytes (comp_type_size already knows it) so
           the stream stays in sync, and render white rather than guess. */
        unsigned int i, n = comp_type_size(GX_VA_CLR0, type);
        for (i = 0; i < n; i++) rd_u8(r);
        if (!warned) {
            warned = 1;
            pc_sys_log("pc_gx_fifo: unsupported vertex color format "
                       "(RGB565/RGBA4/RGBA6); rendering white\n");
        }
        out[0] = out[1] = out[2] = out[3] = 1.0f;
        break;
    }
    }
}

static float read_direct_component(reader_t* r, unsigned char type, unsigned char frac)
{
    switch (type) {
    case GX_U8:  return (float) rd_u8(r) / (float) (1u << frac);
    case GX_S8:  return (float) (signed char) rd_u8(r) / (float) (1u << frac);
    case GX_U16: return (float) rd_u16(r) / (float) (1u << frac);
    case GX_S16: return (float) (short) rd_u16(r) / (float) (1u << frac);
    case GX_F32: return rd_f32(r);
    default:     return 0.0f;
    }
}

/* Reads and discards one attribute's DIRECT-mode bytes without decoding
   them -- for attributes this milestone doesn't act on yet (normals,
   texcoords, color1, matrix indices), which still have to be read to keep
   the stream aligned for POS/CLR0 later in the same vertex. */
static void skip_direct(reader_t* r, GXAttr attr, const pc_vat_fmt_t* vf)
{
    unsigned int n = attr_direct_size(attr, vf);
    rd_skip(r, n);
}

/* Vulkan objects for the fixed pipeline -- created once, lazily, the first
   time a draw actually happens (GXInit runs long before any vertex format
   is configured, so there is nothing to build a pipeline against yet). */
static VkPipelineLayout pipeline_layout;
static VkPipeline pipeline;
static VkBuffer vertex_vbo;
static VkDeviceMemory vertex_vbo_mem;
static int pipeline_ready;

static unsigned int find_memory_type(VkPhysicalDevice phys, unsigned int type_bits,
                                     VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties props;
    unsigned int i;
    vkGetPhysicalDeviceMemoryProperties(phys, &props);
    for (i = 0; i < props.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    return 0;
}

static int ensure_pipeline(void)
{
    VkDevice dev;
    VkShaderModuleCreateInfo smci;
    VkShaderModule vert_mod, frag_mod;
    VkPipelineShaderStageCreateInfo stages[2];
    VkVertexInputBindingDescription binding;
    VkVertexInputAttributeDescription attrs[2];
    VkPipelineVertexInputStateCreateInfo vin;
    VkPipelineInputAssemblyStateCreateInfo ia;
    VkPipelineViewportStateCreateInfo vp;
    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo ms;
    VkPipelineColorBlendAttachmentState cba;
    VkPipelineColorBlendStateCreateInfo cb;
    VkDynamicState dyn_states[2];
    VkPipelineDynamicStateCreateInfo dyn;
    VkPushConstantRange pcr;
    VkPipelineLayoutCreateInfo plci;
    VkGraphicsPipelineCreateInfo gpci;
    VkBufferCreateInfo bci;
    VkMemoryRequirements mreq;
    VkMemoryAllocateInfo mai;

    if (pipeline_ready) {
        return 0;
    }
    dev = pc_vulkan_device();

    memset(&smci, 0, sizeof smci);
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = gx_basic_vert_spv_len;
    smci.pCode = (const unsigned int*) gx_basic_vert_spv;
    if (vkCreateShaderModule(dev, &smci, NULL, &vert_mod) != VK_SUCCESS) {
        pc_sys_log("pc_gx_fifo: vertex shader module creation failed\n");
        return 1;
    }
    smci.codeSize = gx_basic_frag_spv_len;
    smci.pCode = (const unsigned int*) gx_basic_frag_spv;
    if (vkCreateShaderModule(dev, &smci, NULL, &frag_mod) != VK_SUCCESS) {
        pc_sys_log("pc_gx_fifo: fragment shader module creation failed\n");
        return 1;
    }

    memset(stages, 0, sizeof stages);
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_mod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_mod;
    stages[1].pName = "main";

    binding.binding = 0;
    binding.stride = sizeof(pc_vertex_t);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = (unsigned int) offsetof(pc_vertex_t, pos);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[1].offset = (unsigned int) offsetof(pc_vertex_t, color);

    memset(&vin, 0, sizeof vin);
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = 2;
    vin.pVertexAttributeDescriptions = attrs;

    memset(&ia, 0, sizeof ia);
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    memset(&vp, 0, sizeof vp);
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    memset(&rs, 0, sizeof rs);
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE; /* GXSetCullMode not tracked yet --
        see pc/GX_RENDERER.md milestone 2; drawing both faces is the safe
        default until it is. */
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    memset(&ms, 0, sizeof ms);
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    memset(&cba, 0, sizeof cba);
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    memset(&cb, 0, sizeof cb);
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    dyn_states[0] = VK_DYNAMIC_STATE_VIEWPORT;
    dyn_states[1] = VK_DYNAMIC_STATE_SCISSOR;
    memset(&dyn, 0, sizeof dyn);
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states;

    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.offset = 0;
    pcr.size = 16 * sizeof(float);

    memset(&plci, 0, sizeof plci);
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(dev, &plci, NULL, &pipeline_layout) != VK_SUCCESS) {
        pc_sys_log("pc_gx_fifo: pipeline layout creation failed\n");
        return 1;
    }

    memset(&gpci, 0, sizeof gpci);
    gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vin;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &dyn;
    gpci.layout = pipeline_layout;
    gpci.renderPass = pc_vulkan_render_pass();
    gpci.subpass = 0;

    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL,
                                  &pipeline) != VK_SUCCESS) {
        pc_sys_log("pc_gx_fifo: graphics pipeline creation failed\n");
        return 1;
    }
    vkDestroyShaderModule(dev, vert_mod, NULL);
    vkDestroyShaderModule(dev, frag_mod, NULL);

    /* Host-visible vertex buffer, big enough for the largest single draw
       this milestone allows (PC_MAX_VERTS) -- simple over efficient, same
       tradeoff pc_vulkan.c's single-frame-in-flight design already made. */
    memset(&bci, 0, sizeof bci);
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = sizeof(vertex_buf);
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &bci, NULL, &vertex_vbo) != VK_SUCCESS) {
        pc_sys_log("pc_gx_fifo: vertex buffer creation failed\n");
        return 1;
    }
    vkGetBufferMemoryRequirements(dev, vertex_vbo, &mreq);
    memset(&mai, 0, sizeof mai);
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mreq.size;
    mai.memoryTypeIndex = find_memory_type(
        pc_vulkan_physical_device(), mreq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(dev, &mai, NULL, &vertex_vbo_mem) != VK_SUCCESS) {
        pc_sys_log("pc_gx_fifo: vertex buffer memory allocation failed\n");
        return 1;
    }
    vkBindBufferMemory(dev, vertex_vbo, vertex_vbo_mem, 0);

    pipeline_ready = 1;
    return 0;
}

/* Uploads vertex_buf[0..count) and issues one draw. cmd is the command
   buffer pc_gx_render.c's ensure_frame() already opened this frame. */
static void flush_draw(VkCommandBuffer cmd, unsigned int count)
{
    void* mapped;
    unsigned int width, height;
    VkViewport viewport;
    VkRect2D scissor;
    float mvp[16];
    int r, c;
    VkDeviceSize offset = 0;

    if (cmd == VK_NULL_HANDLE || count == 0 || ensure_pipeline()) {
        return;
    }

    if (count > PC_MAX_VERTS - frame_vertices) {
        pc_sys_log("pc_gx_fifo: per-frame vertex buffer exhausted; draw skipped\n");
        return;
    }
    offset = (VkDeviceSize)frame_vertices * sizeof(pc_vertex_t);

    if (vkMapMemory(pc_vulkan_device(), vertex_vbo_mem, 0,
                    VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
        pc_sys_log("pc_gx_fifo: vertex mapping failed\n"); return;
    }
    memcpy((unsigned char*)mapped + offset, vertex_buf, count * sizeof(pc_vertex_t));
    vkUnmapMemory(pc_vulkan_device(), vertex_vbo_mem);

    /* proj_mtx * [pos_mtx; 0 0 0 1], packed column-major for GLSL's mat4.
       Vertices are pre-transformed by pos_mtx on the CPU (see
       mtx_transform in the draw loop below), so this only carries the
       projection -- kept as a full 4x4 push constant anyway so a future
       milestone that stops pre-transforming can drop the CPU-side matrix
       multiply without touching the shader. */
    for (c = 0; c < 4; c++) {
        for (r = 0; r < 4; r++) {
            /* GX clip depth is [-w,0]; Vulkan is [0,w]. Positive Vulkan
               viewport height also reverses GX's upward screen Y. */
            mvp[c * 4 + r] = (r == 1 || r == 2) ? -proj_mtx[r][c] : proj_mtx[r][c];
        }
    }

    pc_vulkan_extent(&width, &height);
    memset(&viewport, 0, sizeof viewport);
    viewport.width = (float) width;
    viewport.height = (float) height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    memset(&scissor, 0, sizeof scissor);
    scissor.extent.width = width;
    scissor.extent.height = height;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof mvp, mvp);
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_vbo, &offset);
    vkCmdDraw(cmd, count, 1, 0, 0);
    pc_vulkan_mark_draw();
    frame_vertices += count;
    {
        static int reported;
        if (!reported) {
            reported = 1;
            pc_sys_log("pc_gx_fifo: first real GX geometry recorded in Vulkan\n");
            pc_sys_log("pc_gx_fifo: texture coordinates/TEV/depth state not applied by the basic pipeline\n");
        }
    }
}

/* ---- draw command decode --------------------------------------------------
 * GX_DRAW_TRIANGLES is emitted as-is; GX_QUADS/STRIP/FAN are expanded to a
 * triangle list on the host, the same shape prepare_idx_buffer() in
 * Aurora's command_processor.cpp uses (ported here without the index
 * buffer, since vertex_buf is small enough to duplicate vertices into
 * instead of indexing them). Lines and points are logged and skipped --
 * genuinely rare in Melee's own geometry per --trace-gx's tallies, and a
 * missing line is a much smaller visible gap than the effort to add a
 * second pipeline topology for it right now. */
static void emit_vertex(unsigned int* count, const float world_pos[3],
                        const float color[4])
{
    float clip_pos[3];
    if (*count >= PC_MAX_VERTS) {
        return;
    }
    mtx_transform(pos_mtx, world_pos, clip_pos);
    vertex_buf[*count].pos[0] = clip_pos[0];
    vertex_buf[*count].pos[1] = clip_pos[1];
    vertex_buf[*count].pos[2] = clip_pos[2];
    vertex_buf[*count].color[0] = color[0];
    vertex_buf[*count].color[1] = color[1];
    vertex_buf[*count].color[2] = color[2];
    vertex_buf[*count].color[3] = color[3];
    (*count)++;
}

static void handle_draw(VkCommandBuffer cmd, reader_t* r, unsigned char cmd_byte)
{
    unsigned char prim = cmd_byte & GX_OPCODE_MASK;
    unsigned char fmt_idx = cmd_byte & GX_VAT_MASK;
    unsigned short vtx_count = rd_u16(r);
    const pc_vat_fmt_t* vf = &vtx_fmt[fmt_idx];
    static float raw_pos[PC_MAX_VERTS][3];
    static float raw_col[PC_MAX_VERTS][4];
    unsigned short i;
    unsigned int out_count = 0;
    GXAttr attr;

    for (i = 0; i < vtx_count && !r->overrun; i++) {
        float p[3] = {0, 0, 0};
        float c[4] = {1, 1, 1, 1};
        int have_pos = 0;

        for (attr = 0; attr <= GX_VA_TEX7; attr++) {
            unsigned char desc = vtx_desc[attr];
            if (desc == GX_NONE) {
                continue;
            }
            if (desc == GX_INDEX8 || desc == GX_INDEX16) {
                unsigned int idx = desc == GX_INDEX8 ? rd_u8(r) : rd_u16(r);
                const pc_gx_array_t* arr = &gx_arrays[attr];
                if (arr->base == NULL) {
                    continue; /* GXSetArray never called for this attr */
                }
                if (attr == GX_VA_POS) {
                    reader_t ar;
                    ar.p = arr->base + (unsigned long) idx * arr->stride;
                    ar.end = ar.p + attr_direct_size(attr, vf);
                    ar.overrun = 0;
                    p[0] = read_direct_component(&ar, vf->attrs[attr].type, vf->attrs[attr].frac);
                    p[1] = read_direct_component(&ar, vf->attrs[attr].type, vf->attrs[attr].frac);
                    if (comp_cnt_count(attr, vf->attrs[attr].cnt) > 2) {
                        p[2] = read_direct_component(&ar, vf->attrs[attr].type, vf->attrs[attr].frac);
                    }
                    have_pos = 1;
                } else if (attr == GX_VA_CLR0) {
                    reader_t ar;
                    ar.p = arr->base + (unsigned long) idx * arr->stride;
                    ar.end = ar.p + comp_type_size(attr, vf->attrs[attr].type);
                    ar.overrun = 0;
                    read_color(&ar, vf->attrs[attr].type, c);
                }
                continue;
            }
            /* GX_DIRECT */
            if (attr == GX_VA_POS) {
                p[0] = read_direct_component(r, vf->attrs[attr].type, vf->attrs[attr].frac);
                p[1] = read_direct_component(r, vf->attrs[attr].type, vf->attrs[attr].frac);
                if (comp_cnt_count(attr, vf->attrs[attr].cnt) > 2) {
                    p[2] = read_direct_component(r, vf->attrs[attr].type, vf->attrs[attr].frac);
                }
                have_pos = 1;
            } else if (attr == GX_VA_CLR0) {
                read_color(r, vf->attrs[attr].type, c);
            } else {
                skip_direct(r, attr, vf);
            }
        }

        if (have_pos) {
            memcpy(raw_pos[out_count], p, sizeof p);
            memcpy(raw_col[out_count], c, sizeof c);
            out_count++;
        }
    }

    if (r->overrun) {
        pc_sys_log("pc_gx_fifo: vertex data ran past the end of the display "
                   "list; draw skipped\n");
        return;
    }

    {
        unsigned int emitted = 0;
        if (prim == GX_TRIANGLES) {
            for (i = 0; i + 2 < out_count; i += 3) {
                emit_vertex(&emitted, raw_pos[i], raw_col[i]);
                emit_vertex(&emitted, raw_pos[i + 1], raw_col[i + 1]);
                emit_vertex(&emitted, raw_pos[i + 2], raw_col[i + 2]);
            }
        } else if (prim == GX_TRIANGLESTRIP) {
            for (i = 2; i < out_count; i++) {
                if (i & 1) {
                    emit_vertex(&emitted, raw_pos[i - 1], raw_col[i - 1]);
                    emit_vertex(&emitted, raw_pos[i - 2], raw_col[i - 2]);
                } else {
                    emit_vertex(&emitted, raw_pos[i - 2], raw_col[i - 2]);
                    emit_vertex(&emitted, raw_pos[i - 1], raw_col[i - 1]);
                }
                emit_vertex(&emitted, raw_pos[i], raw_col[i]);
            }
        } else if (prim == GX_TRIANGLEFAN) {
            for (i = 2; i < out_count; i++) {
                emit_vertex(&emitted, raw_pos[0], raw_col[0]);
                emit_vertex(&emitted, raw_pos[i - 1], raw_col[i - 1]);
                emit_vertex(&emitted, raw_pos[i], raw_col[i]);
            }
        } else if (prim == GX_QUADS) {
            for (i = 0; i + 3 < out_count; i += 4) {
                emit_vertex(&emitted, raw_pos[i], raw_col[i]);
                emit_vertex(&emitted, raw_pos[i + 1], raw_col[i + 1]);
                emit_vertex(&emitted, raw_pos[i + 2], raw_col[i + 2]);
                emit_vertex(&emitted, raw_pos[i + 2], raw_col[i + 2]);
                emit_vertex(&emitted, raw_pos[i + 3], raw_col[i + 3]);
                emit_vertex(&emitted, raw_pos[i], raw_col[i]);
            }
        } else {
            static int warned;
            if (!warned) {
                warned = 1;
                pc_sys_log("pc_gx_fifo: GX_LINES/LINESTRIP/POINTS not drawn "
                          "yet (logged once)\n");
            }
            return;
        }
        flush_draw(cmd, emitted);
    }
}

/* ---- entry point ----------------------------------------------------------
 * Called from __wrap_GXCallDisplayList (pc_gx_render.c). cmd is the frame's
 * open command buffer, or VK_NULL_HANDLE if the window failed to init --
 * in which case this still walks the stream (for state-tracking
 * consistency) but flush_draw() below no-ops without one. */
void pc_gx_fifo_exec(VkCommandBuffer cmd, const void* data, unsigned int nbytes)
{
    reader_t r;
    r.p = (const unsigned char*) data;
    r.end = r.p + nbytes;
    r.overrun = 0;

    while (r.p < r.end && !r.overrun) {
        unsigned char cmd_byte = rd_u8(&r);
        unsigned char opcode = cmd_byte & GX_OPCODE_MASK;

        if (cmd_byte == GX_NOP) {
            continue;
        }
        if (cmd_byte == GX_LOAD_BP_REG) {
            rd_skip(&r, 4); /* TEV/blend state -- task 10 */
            continue;
        }
        if (cmd_byte == GX_LOAD_CP_REG) {
            unsigned char addr = rd_u8(&r);
            unsigned int value = rd_u32(&r);
            handle_cp_reg(addr, value);
            continue;
        }
        if (cmd_byte == GX_LOAD_XF_REG) {
            unsigned int header = rd_u32(&r);
            unsigned int count = ((header >> 16) & 0xFFFFu) + 1u;
            rd_skip(&r, count * 4u); /* matrices via GXLoadPosMtxImm/
                GXSetProjection already capture what this milestone needs;
                other XF state (lighting, texgen) is skipped, not decoded */
            continue;
        }
        if (cmd_byte == GX_LOAD_INDX_A || cmd_byte == GX_LOAD_INDX_B ||
            cmd_byte == GX_LOAD_INDX_C || cmd_byte == GX_LOAD_INDX_D) {
            static int warned;
            rd_skip(&r, 4); /* header only; the data comes from an array,
                not the stream, so there is nothing else to skip here */
            if (!warned) {
                warned = 1;
                pc_sys_log("pc_gx_fifo: indexed XF load (skinning matrix) "
                          "not applied yet (logged once)\n");
            }
            continue;
        }
        if (cmd_byte == 0x40) { /* CALL_DL, nested -- not supported */
            static int warned;
            rd_skip(&r, 8);
            if (!warned) {
                warned = 1;
                pc_sys_log("pc_gx_fifo: nested GXCallDisplayList ignored "
                          "(logged once)\n");
            }
            continue;
        }
        if (cmd_byte == 0x48) { /* invalidate vertex cache */
            continue;
        }
        if (opcode == GX_DRAW_QUADS || opcode == GX_DRAW_TRIANGLES ||
            opcode == GX_DRAW_TRIANGLE_STRIP || opcode == GX_DRAW_TRIANGLE_FAN ||
            opcode == GX_DRAW_LINES || opcode == GX_DRAW_LINE_STRIP ||
            opcode == GX_DRAW_POINTS) {
            handle_draw(cmd, &r, cmd_byte);
            continue;
        }

        pc_sys_log("pc_gx_fifo: unknown opcode; stopping this display "
                   "list rather than guess\n");
        return;
    }
}

/* The game's release-mode GXPosition/GXColor/GXTexCoord helpers are inline
   MMIO stores. Renderer-only header hooks serialize their native values to
   the same big-endian command stream that display lists use. */
#define PC_IMMEDIATE_BYTES (16u * 1024u * 1024u)
static unsigned char immediate[PC_IMMEDIATE_BYTES];
static unsigned immediate_size, immediate_expected;
static VkCommandBuffer immediate_cmd;

void pc_gx_immediate_end(void)
{
    if (immediate_expected) {
        pc_sys_log("pc_gx_fifo: incomplete immediate primitive; discarded\n");
        immediate_expected = 0;
    }
}

void pc_gx_immediate_begin(VkCommandBuffer cmd, unsigned type, unsigned fmt, unsigned count)
{
    unsigned attr, stride = 0;
    pc_gx_immediate_end();
    if (fmt >= 8 || count > 65535 || !count) return;
    for (attr = 0; attr <= GX_VA_TEX7; ++attr) {
        switch (vtx_desc[attr]) {
        case GX_DIRECT: stride += attr_direct_size((GXAttr)attr, &vtx_fmt[fmt]); break;
        case GX_INDEX8: ++stride; break;
        case GX_INDEX16: stride += 2; break;
        }
    }
    if (!stride || count > (PC_IMMEDIATE_BYTES - 3) / stride) {
        pc_sys_log("pc_gx_fifo: invalid or oversized immediate format\n"); return;
    }
    immediate_cmd = cmd;
    immediate[0] = (unsigned char)(type | fmt);
    immediate[1] = (unsigned char)(count >> 8); immediate[2] = (unsigned char)count;
    immediate_size = 3; immediate_expected = 3 + count * stride;
}

void pc_gx_immediate_write(const void* value, unsigned size)
{
    unsigned bits = 0, i;
    if (!immediate_expected) return;
    if ((size != 1 && size != 2 && size != 4) || size > immediate_expected - immediate_size) {
        pc_sys_log("pc_gx_fifo: immediate write exceeds primitive; discarded\n");
        immediate_expected = 0; return;
    }
    memcpy(&bits, value, size);
    for (i = 0; i < size; ++i)
        immediate[immediate_size++] = (unsigned char)(bits >> (8 * (size - i - 1)));
    if (immediate_size == immediate_expected) {
        immediate_expected = 0;
        pc_gx_fifo_exec(immediate_cmd, immediate, immediate_size);
    }
}
