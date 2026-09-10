#version 450

layout(location = 0) in vec4 vColor;
layout(location = 2) in vec4 vColor1;
layout(location = 1) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set=0, binding=0) uniform sampler2D gxTextures[8];
layout(std140, set=0, binding=1) uniform Material {
    vec4 initialRegisters[4];
    vec4 konst[4];
    uvec4 stages[4];
    uint count;
    uint compare;
    uvec2 reserved;
} pc;
vec4 registers[4];

// Constant sampler indices do not require descriptor-indexing device features.
vec4 sampleTexture(uint slot) {
    switch(slot) {
    case 0: return texture(gxTextures[0], vUV);
    case 1: return texture(gxTextures[1], vUV);
    case 2: return texture(gxTextures[2], vUV);
    case 3: return texture(gxTextures[3], vUV);
    case 4: return texture(gxTextures[4], vUV);
    case 5: return texture(gxTextures[5], vUV);
    case 6: return texture(gxTextures[6], vUV);
    case 7: return texture(gxTextures[7], vUV);
    default: return vec4(0);
    }
}

vec3 colorInput(uint n, vec4 t, vec4 k, vec4 raster) {
    if (n < 8) return (n & 1) == 0 ? registers[n/2].rgb : registers[n/2].aaa;
    switch(n) {
    case 8: return t.rgb;
    case 9: return t.aaa;
    case 10: return raster.rgb;
    case 11: return raster.aaa;
    case 12: return vec3(1);
    case 13: return vec3(0.5);
    case 14: return k.rgb;
    default: return vec3(0);
    }
}
float alphaInput(uint n, vec4 t, vec4 k, vec4 raster) {
    if(n < 4) return registers[n].a;
    if(n == 4) return t.a;
    if(n == 5) return raster.a;
    if(n == 6) return k.a;
    return 0;
}
vec3 operation(vec3 a, vec3 b, vec3 c, vec3 d, uint op) {
    vec3 v = mix(a, b, c);
    v = d + (((op >> 18) & 1) != 0 ? -v : v);
    uint bias = (op >> 16) & 3;
    v += bias == 1 ? 0.5 : (bias == 2 ? -0.5 : 0.0);
    uint scale = (op >> 20) & 3;
    v *= scale == 3 ? 0.5 : float(1 << scale);
    return ((op >> 19) & 1) != 0 ? clamp(v, 0, 1) : clamp(v, -4, 4);
}
bool comparison(uint op, float a, float refValue) {
    switch(op) {
    case 0: return false;
    case 1: return a < refValue;
    case 2: return a == refValue;
    case 3: return a <= refValue;
    case 4: return a > refValue;
    case 5: return a != refValue;
    case 6: return a >= refValue;
    default: return true;
    }
}

void main() {
    for (uint i = 0; i < 4; ++i) registers[i] = pc.initialRegisters[i];
    for (uint i = 0; i < pc.count; ++i) {
        uvec4 stage = pc.stages[i];
        uint c = stage.x, a = stage.y;
        vec4 t = sampleTexture(stage.z), k = pc.konst[i];
        /* The BP raster field, as GXTev.c's c2r[] encodes it: 0 selects
           colour channel 0, 1 selects channel 1 -- which the vertex stage
           supplies already lit, because GX lights per vertex -- and 7 is the
           zero channel. */
        vec4 raster = stage.w == 0 ? vColor : (stage.w == 1 ? vColor1 : vec4(0));
        vec3 rgb = operation(colorInput((c>>12)&15,t,k,raster), colorInput((c>>8)&15,t,k,raster),
                             colorInput((c>>4)&15,t,k,raster), colorInput(c&15,t,k,raster), c);
        float alpha = operation(vec3(alphaInput((a>>13)&7,t,k,raster)), vec3(alphaInput((a>>10)&7,t,k,raster)),
                                 vec3(alphaInput((a>>7)&7,t,k,raster)), vec3(alphaInput((a>>4)&7,t,k,raster)), a).x;
        // Evaluate both operations before either write; RGB/alpha destinations differ.
        registers[(c>>22)&3].rgb = rgb;
        registers[(a>>22)&3].a = alpha;
    }
    vec3 rgb = registers[0].rgb;
    float alpha = registers[0].a;
    float alpha8 = round(clamp(alpha, 0, 1) * 255.0);
    bool p = comparison((pc.compare>>16)&7, alpha8, float(pc.compare&255));
    bool q = comparison((pc.compare>>19)&7, alpha8, float((pc.compare>>8)&255));
    uint logic = (pc.compare>>22)&3;
    if (!(logic == 0 ? p && q : logic == 1 ? p || q : logic == 2 ? p != q : p == q)) discard;
    outColor = clamp(vec4(rgb, alpha), 0, 1);
}
